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

    func testCurrentMetadataAndHistoryAreReadOnlyAndDistinct() throws {
        try fixture { store, _ in
            try store.seedCheckpoint()
            let old = try XCTUnwrap(try store.checkpoints().first)
            try store.seedCheckpoint()
            let pre = store.user.appendingPathComponent("quicksaves/CUSA00207/before-load-" + String(repeating: "a", count: 32))
            try FileManager.default.copyItem(at: store.checkpointFolder(old), to: pre)
            let before = try SaveStore.inventory(store.user).files
            let live = try XCTUnwrap(try store.currentGameSave())
            XCTAssertEqual(live.folder, store.user.appendingPathComponent("savedata"))
            XCTAssertEqual(live.files, 2); XCTAssertEqual(live.bytes, 16); XCTAssertNotNil(live.modified)
            let checkpoints = try store.checkpoints()
            XCTAssertEqual(checkpoints.count, 3)
            XCTAssertEqual(checkpoints.filter(\.current).count, 1)
            XCTAssertEqual(checkpoints.filter { !$0.current }.count, 2)
            XCTAssertEqual(checkpoints.filter(\.beforeRestore).count, 1)
            XCTAssertTrue(checkpoints.allSatisfy { $0.bytes == 16 })
            XCTAssertEqual(try SaveStore.inventory(store.user).files, before)
            XCTAssertFalse(FileManager.default.fileExists(atPath: store.directory.path))
        }
    }

    func testEmptyReadDoesNotCreateFoldersOrDiscoverSiblingQA() throws {
        try fixture { store, root in
            let missing = root.appendingPathComponent("empty-user")
            let empty = SaveStore(user: missing, directory: root.appendingPathComponent("empty-backups"))
            XCTAssertNil(try empty.currentGameSave()); XCTAssertTrue(try empty.checkpoints().isEmpty)
            XCTAssertTrue(try empty.backups().isEmpty)
            XCTAssertFalse(FileManager.default.fileExists(atPath: missing.path))
            XCTAssertFalse(FileManager.default.fileExists(atPath: empty.directory.path))
            let qa = root.appendingPathComponent("qa/user")
            try FileManager.default.createDirectory(at: qa, withIntermediateDirectories: true)
            try FileManager.default.copyItem(at: store.user.appendingPathComponent("savedata"), to: qa.appendingPathComponent("savedata"))
            XCTAssertNil(try empty.currentGameSave())
        }
    }

    func testCurrentBackupKindsAndSelectedCheckpointDoNotChangeActiveSaves() throws {
        try fixture { store, _ in
            try store.seedCheckpoint()
            let old = try XCTUnwrap(try store.checkpoints().first)
            try store.seedCheckpoint()
            try store.locked { }
            let before = try SaveStore.inventory(store.user).files
            try store.locked { try store.backupCurrentGameSave() }
            try store.locked { try store.backupCheckpoint(try XCTUnwrap(try store.checkpoints().first { $0.id == old.id })) }
            let backups = try store.backups()
            XCTAssertEqual(backups.count, 2)
            let normal = try XCTUnwrap(backups.first { $0.kinds == ["savedata"] })
            XCTAssertEqual(try SaveStore.inventory(store.verify(normal).appendingPathComponent("savedata")).files,
                           try SaveStore.inventory(store.user.appendingPathComponent("savedata")).files)
            let quick = try XCTUnwrap(backups.first { $0.kinds == ["quicksaves"] })
            let payload = try store.verify(quick)
            let exported = SaveStore(user: payload, directory: payload.appendingPathComponent("unused"))
            XCTAssertEqual(try exported.checkpoints().count, 1)
            XCTAssertEqual(try exported.checkpoints().first?.id, old.id)
            XCTAssertEqual(try exported.checkpoints().first?.current, true)
            XCTAssertEqual(try SaveStore.inventory(store.user).files, before)
        }
    }

    func testExportsAreCompleteUniqueAndDoNotCreateBackupsOrReplaceFiles() throws {
        try fixture { store, root in
            let metadata = store.user.appendingPathComponent("savedata/1/CUSA00207/SPRJ0005.sce_sys")
            try FileManager.default.createDirectory(at: metadata, withIntermediateDirectories: true)
            try Data("icon".utf8).write(to: metadata.appendingPathComponent("icon0.png"))
            try store.locked { }
            let before = try SaveStore.inventory(store.user).files
            let exports = root.appendingPathComponent("exports")
            try FileManager.default.createDirectory(at: exports, withIntermediateDirectories: true)
            let a = try store.locked { try store.exportCurrentGameSave(to: exports) }
            let b = try store.locked { try store.exportCurrentGameSave(to: exports) }
            XCTAssertNotEqual(a, b)
            XCTAssertEqual(try SaveStore.inventory(a.appendingPathComponent("savedata")).files,
                           try SaveStore.inventory(store.user.appendingPathComponent("savedata")).files)
            XCTAssertEqual(try SaveStore.inventory(a).files, try SaveStore.inventory(b).files)
            XCTAssertTrue(FileManager.default.fileExists(atPath: a.appendingPathComponent("savedata/1/CUSA00207/SPRJ0005.sce_sys/icon0.png").path))
            XCTAssertEqual(try SaveStore.inventory(store.user).files, before)
            XCTAssertFalse(FileManager.default.fileExists(atPath: store.directory.path))
            XCTAssertFalse(try FileManager.default.contentsOfDirectory(atPath: exports.path).contains { $0.hasPrefix(".") })
        }
    }

    func testPreRestoreExportUsesCompatibleCheckpointAndKeepsLivePointer() throws {
        try fixture { store, root in
            try store.seedCheckpoint()
            let original = try XCTUnwrap(try store.checkpoints().first)
            let pre = store.user.appendingPathComponent("quicksaves/CUSA00207/before-load-" + String(repeating: "b", count: 32))
            try FileManager.default.copyItem(at: store.checkpointFolder(original), to: pre)
            let checkpoint = try XCTUnwrap(try store.checkpoints().first { $0.beforeRestore })
            try store.locked { }
            let before = try SaveStore.inventory(store.user).files
            let exports = root.appendingPathComponent("exports")
            try FileManager.default.createDirectory(at: exports, withIntermediateDirectories: true)
            let target = try store.locked { try store.exportCheckpoint(checkpoint, to: exports) }
            let exported = SaveStore(user: target, directory: target.appendingPathComponent("unused"))
            XCTAssertEqual(try exported.checkpoints().count, 1)
            XCTAssertEqual(try exported.checkpoints().first?.id, "save-" + String(repeating: "b", count: 32))
            XCTAssertEqual(try exported.checkpoints().first?.current, true)
            let repo = URL(fileURLWithPath: #filePath).deletingPathExtension().deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
            let process = Process(); process.executableURL = URL(fileURLWithPath: "/usr/bin/env")
            process.arguments = ["python3", repo.appendingPathComponent("tools/macos/quicksave.py").path, "check", target.path, "CUSA00207"]
            try process.run(); process.waitUntilExit(); XCTAssertEqual(process.terminationStatus, 0)
            XCTAssertEqual(try SaveStore.inventory(store.user).files, before)
        }
    }

    func testStaleCurrentCheckpointAndCorruptedDataCannotBeExportedOrArchived() throws {
        try fixture { store, root in
            try store.seedCheckpoint()
            let stale = try XCTUnwrap(try store.checkpoints().first)
            try store.seedCheckpoint()
            let exports = root.appendingPathComponent("exports")
            try FileManager.default.createDirectory(at: exports, withIntermediateDirectories: true)
            XCTAssertThrowsError(try store.exportCheckpoint(stale, to: exports))
            XCTAssertThrowsError(try store.backupCheckpoint(stale))
            let active = try XCTUnwrap(try store.checkpoints().first { $0.current })
            try Data("corrupt".utf8).write(to: store.checkpointFolder(active).appendingPathComponent("data/SPRJ0005/userdata0000"))
            let before = try SaveStore.inventory(store.user).files
            XCTAssertThrowsError(try store.exportCheckpoint(active, to: exports))
            XCTAssertThrowsError(try store.backupCheckpoint(active))
            XCTAssertTrue(try FileManager.default.contentsOfDirectory(atPath: exports.path).isEmpty)
            XCTAssertFalse(FileManager.default.fileExists(atPath: store.directory.path))
            XCTAssertEqual(try SaveStore.inventory(store.user).files, before)
        }
    }

    func testExportDestinationsCannotPolluteLiveSavesOrArchivesThroughSymlinks() throws {
        try fixture { store, root in
            try store.locked { }
            try FileManager.default.createDirectory(at: store.directory, withIntermediateDirectories: true)
            let alias = root.appendingPathComponent("alias")
            try FileManager.default.createSymbolicLink(at: alias, withDestinationURL: store.user)
            let before = try SaveStore.inventory(store.user).files
            for parent in [store.user, store.user.appendingPathComponent("savedata"), store.directory, alias] {
                XCTAssertThrowsError(try store.locked { try store.exportCurrentGameSave(to: parent) })
            }
            XCTAssertEqual(try SaveStore.inventory(store.user).files, before)
            XCTAssertTrue(try FileManager.default.contentsOfDirectory(atPath: store.directory.path).isEmpty)
        }
    }

    func testMetadataRejectsSymlinkAndOversizedManifestWithoutWriting() throws {
        try fixture { store, root in
            try store.seedCheckpoint()
            let checkpoint = try XCTUnwrap(try store.checkpoints().first)
            let file = try store.checkpointFolder(checkpoint).appendingPathComponent("manifest.json")
            try Data(repeating: 32, count: 1024 * 1024 + 1).write(to: file)
            let before = try SaveStore.inventory(store.user).files
            XCTAssertThrowsError(try store.checkpoints())
            XCTAssertEqual(try SaveStore.inventory(store.user).files, before)
            try FileManager.default.removeItem(at: file)
            let outside = root.appendingPathComponent("outside.json")
            try Data("{}".utf8).write(to: outside)
            try FileManager.default.createSymbolicLink(at: file, withDestinationURL: outside)
            XCTAssertThrowsError(try store.checkpoints())
            try FileManager.default.createSymbolicLink(at: store.user.appendingPathComponent("savedata/link"), withDestinationURL: root)
            XCTAssertThrowsError(try store.currentGameSave())
            XCTAssertFalse(FileManager.default.fileExists(atPath: store.directory.path))
        }
    }

    func testUnpublishedCheckpointDoesNotHideTheActiveCheckpoint() throws {
        try fixture { store, _ in
            try store.seedCheckpoint()
            let active = try XCTUnwrap(try store.checkpoints().first)
            let unfinished = store.user.appendingPathComponent("quicksaves/CUSA00207/save-" + String(repeating: "c", count: 32))
            try FileManager.default.createDirectory(at: unfinished, withIntermediateDirectories: true)
            XCTAssertEqual(try store.checkpoints().map(\.id), [active.id])
            XCTAssertEqual(try store.checkpoints().first?.current, true)
        }
    }

}
