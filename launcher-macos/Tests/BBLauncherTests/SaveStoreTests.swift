import XCTest
import Darwin
@testable import BBLauncher

final class SaveStoreTests: XCTestCase {
    func fixture(_ body: (SaveStore, URL) throws -> Void) throws {
        let root = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: root) }
        let user = root.appendingPathComponent("user")
        let save = user.appendingPathComponent("savedata/1/CUSA00207/SPRJ0005")
        try FileManager.default.createDirectory(at: save, withIntermediateDirectories: true)
        for name in ["userdata0000", "userdata0010"] { try Data("original".utf8).write(to: save.appendingPathComponent(name)) }
        try body(SaveStore(user: user, directory: root.appendingPathComponent("backups")), root)
    }

    func testOnlineArchivesAndSeedsThenManualOfflineSeedsAnInteroperableCheckpoint() throws {
        try fixture { store, _ in
            try store.seedCheckpoint()
            let quick = store.user.appendingPathComponent("quicksaves")
            let before = try SaveStore.inventory(quick).files
            let saves = try SaveStore.inventory(store.user.appendingPathComponent("savedata")).files
            try store.prepareMode(online: true)
            XCTAssertTrue(FileManager.default.fileExists(atPath: quick.path))
            let archived = try XCTUnwrap(try store.backups().first)
            XCTAssertEqual(try SaveStore.inventory(store.verify(archived).appendingPathComponent("quicksaves")).files, before)
            let count = try store.backups().count
            try store.prepareMode(online: true)
            XCTAssertEqual(try store.backups().count, count)
            try Data("online progress".utf8).write(to: store.user.appendingPathComponent("savedata/1/CUSA00207/SPRJ0005/userdata0000"))
            try store.prepareMode(online: false)
            let title = quick.appendingPathComponent("CUSA00207")
            let pointer = try XCTUnwrap(JSONSerialization.jsonObject(with: Data(contentsOf: title.appendingPathComponent("current.json"))) as? [String: String])
            let folder = title.appendingPathComponent(try XCTUnwrap(pointer["checkpoint"]))
            XCTAssertEqual(try Data(contentsOf: folder.appendingPathComponent("data/SPRJ0005/userdata0000")), Data("online progress".utf8))
            let manifest = try XCTUnwrap(JSONSerialization.jsonObject(with: Data(contentsOf: folder.appendingPathComponent("manifest.json"))) as? [String: Any])
            let files = try XCTUnwrap(manifest["files"] as? [String: [String: Any]])
            XCTAssertEqual(files["SPRJ0005/userdata0000"]?["size"] as? Int, 15)
            XCTAssertNotEqual(try SaveStore.inventory(store.user.appendingPathComponent("savedata")).files, saves)
            let data = try store.backup(label: "checkpoint verification", kinds: ["quicksaves"])
            try store.restore(data, kind: "quicksaves")
            XCTAssertTrue(store.hasOfflineRestoreIntent)
        }
    }

    func testRestoreVerifiesBeforeReplacingAndKeepsOriginalBackup() throws {
        try fixture { store, _ in
            let record = try store.backup(label: "original")
            let file = store.user.appendingPathComponent("savedata/1/CUSA00207/SPRJ0005/userdata0000")
            try Data("new".utf8).write(to: file)
            try store.restore(record, kind: "savedata")
            XCTAssertEqual(try Data(contentsOf: file), Data("original".utf8))
            XCTAssertEqual(try store.backups().count, 2)
            let payload = try store.verify(record)
            try Data("damaged".utf8).write(to: payload.appendingPathComponent("savedata/1/CUSA00207/SPRJ0005/userdata0000"))
            XCTAssertThrowsError(try store.restore(record, kind: "savedata"))
            XCTAssertEqual(try Data(contentsOf: file), Data("original".utf8))
        }
    }

    func testLegacyOnlineProgressIsCopiedOnceWithoutDeletingEitherSource() throws {
        try fixture { store, root in
            let legacy = root.appendingPathComponent("legacy")
            try FileManager.default.createDirectory(at: legacy, withIntermediateDirectories: true)
            try FileManager.default.copyItem(at: store.user.appendingPathComponent("savedata"), to: legacy.appendingPathComponent("savedata"))
            try "Blood Echoes isolated online saves v1\n".write(to: legacy.appendingPathComponent(".blood-echoes-online-save"), atomically: true, encoding: .utf8)
            let file = "savedata/1/CUSA00207/SPRJ0005/userdata0000"
            try Data("online".utf8).write(to: legacy.appendingPathComponent(file))
            try store.prepareMode(online: true, legacy: legacy)
            XCTAssertEqual(try Data(contentsOf: store.user.appendingPathComponent(file)), Data("online".utf8))
            XCTAssertEqual(try Data(contentsOf: legacy.appendingPathComponent(file)), Data("online".utf8))
            try Data("later shared".utf8).write(to: store.user.appendingPathComponent(file))
            try store.prepareMode(online: true, legacy: legacy)
            XCTAssertEqual(try Data(contentsOf: store.user.appendingPathComponent(file)), Data("later shared".utf8))
        }
    }

    func testLockAndSymlinkFailuresPreserveCurrentFiles() throws {
        try fixture { store, root in
            let before = try SaveStore.inventory(store.user.appendingPathComponent("savedata")).files
            let fd = Darwin.open(store.user.appendingPathComponent(".quicksave-session.lock").path, O_RDWR | O_CREAT, 0o600)
            defer { Darwin.close(fd) }
            XCTAssertEqual(flock(fd, LOCK_EX | LOCK_NB), 0)
            XCTAssertThrowsError(try store.prepareMode(online: true))
            flock(fd, LOCK_UN)
            try FileManager.default.createSymbolicLink(at: store.user.appendingPathComponent("savedata/link"), withDestinationURL: root)
            XCTAssertThrowsError(try store.backup(label: "bad"))
            try FileManager.default.removeItem(at: store.user.appendingPathComponent("savedata/link"))
            XCTAssertEqual(try SaveStore.inventory(store.user.appendingPathComponent("savedata")).files, before)
        }
    }

    func testQuickCheckpointRestoreAndPythonManifestCompatibility() throws {
        try fixture { store, _ in
            try store.seedCheckpoint()
            let checkpoint = try XCTUnwrap(try store.checkpoints().first)
            let file = store.user.appendingPathComponent("savedata/1/CUSA00207/SPRJ0005/userdata0000")
            try Data("later".utf8).write(to: file)
            try store.restoreCheckpoint(checkpoint)
            XCTAssertEqual(try Data(contentsOf: file), Data("original".utf8))
            let repo = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
            let process = Process(); process.executableURL = URL(fileURLWithPath: "/usr/bin/env")
            process.arguments = ["python3", repo.appendingPathComponent("tools/macos/quicksave.py").path,
                                 "check", store.user.path, "CUSA00207"]
            try process.run(); process.waitUntilExit()
            XCTAssertEqual(process.terminationStatus, 0)
        }
    }

    func testFormalModeNeverDiscoversSiblingQASaves() throws {
        try fixture { store, root in
            let before = try SaveStore.inventory(store.user.appendingPathComponent("savedata")).files
            let qa = root.appendingPathComponent("qa/optimized-launcher/online-user/savedata")
            try FileManager.default.createDirectory(at: qa, withIntermediateDirectories: true)
            try Data("QA must not be imported".utf8).write(to: qa.appendingPathComponent("save"))
            try store.prepareMode(online: true, legacy: root.appendingPathComponent("optimized-launcher/online-user"))
            XCTAssertEqual(try SaveStore.inventory(store.user.appendingPathComponent("savedata")).files, before)
            XCTAssertEqual(try Data(contentsOf: qa.appendingPathComponent("save")), Data("QA must not be imported".utf8))
            XCTAssertEqual(try store.backups().count, 1)
        }
    }

    func testInterruptedQuickloadIsRecoveredBeforeOnlineClearsItsJournal() throws {
        try fixture { store, _ in
            let live = store.user.appendingPathComponent("savedata/1/CUSA00207")
            let records = Dictionary(uniqueKeysWithValues: try SaveStore.inventory(live).files.map { ($0.key, ["sha256": $0.value, "size": 8] as [String: Any]) })
            let quick = store.user.appendingPathComponent("quicksaves/CUSA00207")
            let name = "before-load-" + String(repeating: "a", count: 32)
            let backup = quick.appendingPathComponent(name)
            try FileManager.default.createDirectory(at: backup, withIntermediateDirectories: true)
            try FileManager.default.moveItem(at: live, to: backup.appendingPathComponent("data"))
            try JSONSerialization.data(withJSONObject: ["before": records, "after": records, "backup": name]).write(to: quick.appendingPathComponent("restore.json"))
            try store.prepareMode(online: true)
            XCTAssertEqual(try Data(contentsOf: live.appendingPathComponent("SPRJ0005/userdata0000")), Data("original".utf8))
            XCTAssertTrue(FileManager.default.fileExists(atPath: quick.path))
            XCTAssertFalse(try store.backups().isEmpty)
        }
    }

    func testOfflineRestoreIntentKeepsRetryCheckpointAndExplicitOnlineCreatesFreshBaseline() throws {
        try fixture { store, _ in
            try store.prepareMode(online: true)
            let target = try XCTUnwrap(try store.checkpoints().first)
            let quick = store.user.appendingPathComponent("quicksaves")
            let baseline = try SaveStore.inventory(quick).files
            // A successful offline F3 restore leaves this before mutating live saves.
            try JSONSerialization.data(withJSONObject: ["version": 1]).write(to: store.offlineRestoreMarker)
            try store.prepareMode(online: false)
            XCTAssertTrue(store.hasOfflineRestoreIntent)
            XCTAssertEqual(try SaveStore.inventory(quick).files, baseline)
            XCTAssertEqual(try store.checkpoints().first?.id, target.id)
            try store.prepareMode(online: false)
            XCTAssertEqual(try SaveStore.inventory(quick).files, baseline)
            let file = store.user.appendingPathComponent("savedata/1/CUSA00207/SPRJ0005/userdata0000")
            try Data("offline retries advanced".utf8).write(to: file)
            try store.prepareMode(online: true)
            XCTAssertNotEqual(try store.checkpoints().first?.id, target.id)
            XCTAssertEqual(try store.checkpoints().count, 1)
        }
    }

    func testEachNewOnlineSessionRefreshesItsBaselineButOrdinaryRefreshDoesNot() throws {
        try fixture { store, _ in
            try store.prepareMode(online: true)
            let first = try XCTUnwrap(try store.checkpoints().first?.id)
            try store.prepareMode(online: true)
            XCTAssertEqual(try store.checkpoints().first?.id, first)
            try store.prepareMode(online: true, newSession: true)
            XCTAssertNotEqual(try store.checkpoints().first?.id, first)
            XCTAssertEqual(try store.checkpoints().count, 1)
            XCTAssertEqual(try store.backups().count, 2)
        }
    }

    func testFailedOnlinePreparationKeepsOldCheckpointAndMode() throws {
        try fixture { store, _ in
            try store.prepareMode(online: false)
            try store.seedCheckpoint()
            let quick = store.user.appendingPathComponent("quicksaves")
            let before = try SaveStore.inventory(quick).files
            let marker = store.user.appendingPathComponent("savedata/unfinished.bbtmp")
            try Data("incomplete write".utf8).write(to: marker)
            XCTAssertThrowsError(try store.prepareMode(online: true))
            XCTAssertEqual(try SaveStore.inventory(quick).files, before)
            XCTAssertEqual(try JSONDecoder().decode(Bool.self, from: Data(contentsOf: store.directory.appendingPathComponent("mode.json"))), false)
        }
    }
}
