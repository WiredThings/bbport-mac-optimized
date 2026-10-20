import XCTest
@testable import BBLauncher

final class SaveRetentionTests: XCTestCase {
    func testThreePerHistoryKindPlusActiveAndPinnedArePreserved() throws {
        try SaveStoreTests().fixture { store, _ in
            for _ in 0..<9 { try store.seedCheckpoint() }
            let first = try XCTUnwrap(try store.checkpoints().last)
            try store.setKept(true, checkpoint: first)
            let original = try store.verifiedCheckpoint(first)
            for i in 0..<8 {
                let name = "before-load-" + UUID().uuidString.replacingOccurrences(of: "-", with: "").lowercased()
                let folder = store.user.appendingPathComponent("quicksaves/CUSA00207/" + name)
                try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
                try FileManager.default.copyItem(at: original, to: folder.appendingPathComponent("data"))
                let manifest = try store.checkpointFolder(first).appendingPathComponent("manifest.json")
                var data = try XCTUnwrap(JSONSerialization.jsonObject(with: Data(contentsOf: manifest)) as? [String: Any])
                data["created"] = Double(i)
                try JSONSerialization.data(withJSONObject: data).write(to: folder.appendingPathComponent("manifest.json"))
            }
            let before = try SaveStore.inventory(store.user.appendingPathComponent("savedata")).files
            let active = try XCTUnwrap(try store.checkpoints().first(where: \.current)).id
            let result = try store.locked { try store.pruneHistory() }
            let after = try store.checkpoints()
            XCTAssertEqual(after.filter { !$0.current && !$0.beforeRestore && !$0.kept }.count, 3)
            XCTAssertEqual(after.filter(\.beforeRestore).count, 3)
            XCTAssertTrue(after.contains { $0.id == first.id && $0.kept })
            XCTAssertTrue(after.contains { $0.id == active && $0.current })
            XCTAssertGreaterThan(result.items, 0)
            XCTAssertEqual(try SaveStore.inventory(store.user.appendingPathComponent("savedata")).files, before)
            XCTAssertEqual(try store.locked { try store.pruneHistory() }.items, 0)
        }
    }

    func testAutomaticArchivesKeepFiveAndNeverRemoveManualOrPinned() throws {
        try SaveStoreTests().fixture { store, _ in
            let manual = try store.backup(label: "Manual backup")
            var records = [SaveBackup]()
            for i in 0..<8 {
                try Data("progress \(i)".utf8).write(to: store.user.appendingPathComponent("savedata/1/CUSA00207/SPRJ0005/userdata0000"))
                records.append(try store.backup(label: "Before online: quicksave archive"))
            }
            try store.setKept(true, backup: records[0])
            let live = try SaveStore.inventory(store.user.appendingPathComponent("savedata")).files
            _ = try store.locked { try store.pruneHistory() }
            let left = try store.backups()
            XCTAssertEqual(left.filter { SaveStore.automatic($0) && $0.kept != true }.count, 5)
            XCTAssertTrue(left.contains { $0.id == records[0].id && $0.kept == true })
            XCTAssertTrue(left.contains { $0.id == manual.id })
            XCTAssertEqual(try SaveStore.inventory(store.user.appendingPathComponent("savedata")).files, live)
        }
    }

    func testAutomaticDedupIgnoresNewCheckpointIDsButManualCopiesRemainDistinct() throws {
        try SaveStoreTests().fixture { store, _ in
            try store.seedCheckpoint()
            let first = try store.backup(label: "Before online: quicksave archive", kinds: ["savedata", "quicksaves"])
            try store.seedCheckpoint()
            let second = try store.backup(label: "Before online: quicksave archive", kinds: ["savedata", "quicksaves"])
            XCTAssertEqual(first.id, second.id)
            XCTAssertNotEqual(try store.backup(label: "Manual backup").id, try store.backup(label: "Manual backup").id)
        }
    }

    func testPendingRestoreOrCorruptHistoryPreventsAnyCleanup() throws {
        try SaveStoreTests().fixture { store, _ in
            for _ in 0..<7 { try store.seedCheckpoint() }
            let root = store.user.appendingPathComponent("quicksaves/CUSA00207")
            try Data("pending".utf8).write(to: root.appendingPathComponent("restore.json"))
            let before = try SaveStore.inventory(root).files
            XCTAssertThrowsError(try store.locked { try store.pruneHistory() })
            XCTAssertEqual(try SaveStore.inventory(root).files, before)
            try FileManager.default.removeItem(at: root.appendingPathComponent("restore.json"))
            let old = try XCTUnwrap(try store.checkpoints().last)
            try Data("broken".utf8).write(to: try store.checkpointFolder(old).appendingPathComponent("data/SPRJ0005/userdata0000"))
            let damaged = try SaveStore.inventory(root).files
            XCTAssertThrowsError(try store.locked { try store.pruneHistory() })
            XCTAssertEqual(try SaveStore.inventory(root).files, damaged)
        }
    }

    func testUnknownArchiveAndLegacyMigrationAreNeverPruned() throws {
        try SaveStoreTests().fixture { store, _ in
            for label in ["Imported save", "Legacy online progress", "Before shared saves", "unknown"] {
                for _ in 0..<7 { _ = try store.backup(label: label) }
            }
            XCTAssertEqual(try store.locked { try store.pruneHistory() }.items, 0)
            XCTAssertEqual(try store.backups().count, 28)
        }
    }

    func testPinnedCheckpointPinsItsModeArchiveBeforeQuickTreeReplacement() throws {
        try SaveStoreTests().fixture { store, _ in
            try store.seedCheckpoint()
            try store.setKept(true, checkpoint: XCTUnwrap(try store.checkpoints().first))
            try store.prepareMode(online: true)
            let old = try XCTUnwrap(try store.backups().first)
            XCTAssertTrue(try XCTUnwrap(try store.backups().first).kept == true)
            for i in 0..<8 {
                try Data("progress \(i)".utf8).write(to: store.user.appendingPathComponent("savedata/1/CUSA00207/SPRJ0005/userdata0000"))
                try store.prepareMode(online: true, newSession: true)
            }
            _ = try store.locked { try store.pruneHistory() }
            XCTAssertTrue(try store.backups().contains { $0.id == old.id })
            try store.setKept(false, backup: old)
            _ = try store.locked { try store.pruneHistory() }
            XCTAssertFalse(try store.backups().contains { $0.id == old.id })
        }
    }

    func testMissingCurrentPointerPreservesAllHistory() throws {
        try SaveStoreTests().fixture { store, _ in
            for _ in 0..<7 { try store.seedCheckpoint() }
            let root = store.user.appendingPathComponent("quicksaves")
            try FileManager.default.removeItem(at: root.appendingPathComponent("CUSA00207/current.json"))
            let before = try SaveStore.inventory(root).files
            XCTAssertThrowsError(try store.locked { try store.pruneHistory() })
            XCTAssertEqual(try SaveStore.inventory(root).files, before)
        }
    }

    func testDryRunReportsOnlyEligibleHistoryWithoutDeleting() throws {
        try SaveStoreTests().fixture { store, _ in
            for _ in 0..<7 { try store.seedCheckpoint() }
            let before = try SaveStore.inventory(store.user.appendingPathComponent("quicksaves")).files
            let plan = try store.locked { try store.pruneHistory(dryRun: true) }
            XCTAssertEqual(plan.items, 3)
            XCTAssertGreaterThan(plan.bytes, 0)
            XCTAssertEqual(try SaveStore.inventory(store.user.appendingPathComponent("quicksaves")).files, before)
            let applied = try store.locked { try store.pruneHistory() }
            XCTAssertEqual(applied.items, plan.items)
            XCTAssertEqual(applied.bytes, plan.bytes)
        }
    }
}
