import XCTest
@testable import BBLauncher

final class CloudSaveTests: XCTestCase {
    let fm = FileManager.default

    func fixture(_ body: (SaveStore, SaveStore, URL) throws -> Void) throws {
        try SaveStoreTests().fixture { first, root in
            let second = SaveStore(user: root.appendingPathComponent("other/user"), directory: root.appendingPathComponent("other/backups"))
            try fm.createDirectory(at: second.user, withIntermediateDirectories: true)
            try fm.copyItem(at: first.user.appendingPathComponent("savedata"), to: second.user.appendingPathComponent("savedata"))
            try body(first, second, root.appendingPathComponent("cloud"))
        }
    }

    func sync(_ store: SaveStore, _ root: URL, choice: CloudSaveChoice? = nil) throws -> CloudSaveResult {
        try store.locked { try CloudSaves(store: store, root: root).synchronize(choice: choice) }
    }

    func progress(_ value: String, _ store: SaveStore) throws {
        try Data(value.utf8).write(to: store.user.appendingPathComponent("savedata/1/CUSA00207/SPRJ0005/userdata0000"))
    }

    func contents(_ store: SaveStore) throws -> [String: String] { try SaveStore.inventory(store.user.appendingPathComponent("savedata")).files }

    func testFreshMacFindsFixedDriveFolderWithoutPathPreferences() throws {
        try fixture { a, b, root in
            let home = root.deletingLastPathComponent().appendingPathComponent("home")
            let shared = CloudSaves.automaticRoot(home: home)
            XCTAssertFalse(fm.fileExists(atPath: home.path)) // resolving is read-only
            try fm.createDirectory(at: shared.deletingLastPathComponent(), withIntermediateDirectories: true)
            try progress("progress from old Mac", a)
            _ = try sync(a, shared)
            XCTAssertEqual(try Data(contentsOf: shared.appendingPathComponent("format.txt")), Data("Blood Echoes normal save sync v1\n".utf8))
            try fm.removeItem(at: b.user.appendingPathComponent("savedata"))
            let imported = try sync(b, CloudSaves.automaticRoot(home: home))
            XCTAssertTrue(imported.imported)
            XCTAssertEqual(try contents(b), try contents(a))
            XCTAssertTrue(b.hasOfflineRestoreIntent)
            XCTAssertEqual(try fm.contentsOfDirectory(at: shared, includingPropertiesForKeys: nil).filter { $0.pathExtension == "bbechoessave" }.count, 1)
        }
    }

    func testUnavailableDriveNeverCreatesFakeCloudFoldersOrChangesProgress() throws {
        try fixture { a, _, root in
            let missingHome = root.deletingLastPathComponent().appendingPathComponent("missing-home")
            let before = try contents(a)
            XCTAssertThrowsError(try sync(a, CloudSaves.automaticRoot(home: missingHome)))
            XCTAssertFalse(fm.fileExists(atPath: missingHome.path))
            XCTAssertEqual(try contents(a), before)
        }
    }

    func testOnlyLatestNormalSavesPublishAndRepeatedSyncDoesNotRewrite() throws {
        try fixture { a, _, root in
            try a.seedCheckpoint()
            _ = try a.backup(label: "Manual backup", kinds: ["savedata", "quicksaves"])
            let normal = try contents(a)
            let quick = try SaveStore.inventory(a.user.appendingPathComponent("quicksaves")).files
            _ = try sync(a, root)
            let first = try SaveStore.inventory(root).files
            for _ in 0..<4 { _ = try sync(a, root) }
            XCTAssertEqual(try SaveStore.inventory(root).files, first)
            XCTAssertFalse(first.keys.contains { $0.contains("quicksaves") || $0.contains("backup") })
            XCTAssertEqual(try contents(a), normal)
            XCTAssertEqual(try SaveStore.inventory(a.user.appendingPathComponent("quicksaves")).files, quick)
            try progress("new progress", a); _ = try sync(a, root)
            XCTAssertEqual(try fm.contentsOfDirectory(at: root, includingPropertiesForKeys: nil).filter { $0.pathExtension == "bbechoessave" }.count, 1)
        }
    }

    func testEqualProgressDoesNotPingPongAndChangedRemoteImportsWithBackup() throws {
        try fixture { a, b, root in
            _ = try sync(a, root); _ = try sync(b, root)
            let initial = try SaveStore.inventory(root).files
            for _ in 0..<3 { _ = try sync(a, root); _ = try sync(b, root) }
            XCTAssertEqual(try SaveStore.inventory(root).files, initial)
            let oldB = try contents(b)
            try progress("played on first Mac", a); _ = try sync(a, root)
            let result = try sync(b, root)
            XCTAssertTrue(result.imported); XCTAssertTrue(result.conflicts.isEmpty)
            XCTAssertEqual(try contents(a), try contents(b))
            let record = try XCTUnwrap(try b.backups().first)
            XCTAssertEqual(try SaveStore.inventory(b.verify(record).appendingPathComponent("savedata")).files, oldB)
            XCTAssertTrue(b.hasOfflineRestoreIntent)
            let noChange = try SaveStore.inventory(root).files
            _ = try sync(a, root); _ = try sync(b, root)
            XCTAssertEqual(try SaveStore.inventory(root).files, noChange)
        }
    }

    func testConcurrentOfflineProgressPreservesBothUntilExplicitChoice() throws {
        try fixture { a, b, root in
            _ = try sync(a, root); _ = try sync(b, root)
            try progress("independent A", a); _ = try sync(a, root)
            try progress("independent B", b)
            let local = try contents(b), cloud = try SaveStore.inventory(root).files
            let result = try sync(b, root)
            XCTAssertEqual(result.conflicts.count, 2)
            XCTAssertEqual(try contents(b), local)
            XCTAssertEqual(try SaveStore.inventory(root).files, cloud)
            let remote = try XCTUnwrap(result.conflicts.first { $0.files == (try? contents(a)) })
            let resolved = try sync(b, root, choice: CloudSaveChoice(device: remote.id, digest: remote.digest))
            XCTAssertTrue(resolved.imported); XCTAssertEqual(try contents(b), try contents(a))
            let original = try XCTUnwrap(try b.backups().first(where: { $0.label == "iCloud conflict backup" }))
            XCTAssertEqual(try SaveStore.inventory(b.verify(original).appendingPathComponent("savedata")).files, local)
            XCTAssertFalse(SaveStore.automatic(original))
            XCTAssertTrue(try sync(a, root).conflicts.isEmpty)
        }
    }

    func testFirstConnectionWithDifferentLocalProgressRequiresChoice() throws {
        try fixture { a, b, root in
            _ = try sync(a, root); try progress("existing different save", b)
            let before = try contents(b)
            XCTAssertEqual(try sync(b, root).conflicts.count, 2)
            XCTAssertEqual(try contents(b), before)
        }
    }

    func testEmptyDeviceCanImportWholeNormalSaveIncludingMetadata() throws {
        try fixture { a, b, root in
            try fm.createDirectory(at: a.user.appendingPathComponent("savedata/1/CUSA00207/sce_sys"), withIntermediateDirectories: true)
            try Data("metadata".utf8).write(to: a.user.appendingPathComponent("savedata/1/CUSA00207/sce_sys/param.sfo"))
            _ = try sync(a, root)
            try fm.removeItem(at: b.user.appendingPathComponent("savedata"))
            XCTAssertTrue(try sync(b, root).imported)
            XCTAssertEqual(try contents(b), try contents(a))
        }
    }

    func testCorruptOrSymlinkedCloudNeverReplacesLocalSaves() throws {
        try fixture { a, b, root in
            _ = try sync(a, root)
            let head = try XCTUnwrap(try fm.contentsOfDirectory(at: root, includingPropertiesForKeys: nil).first { $0.pathExtension == "bbechoessave" })
            let payload = head.appendingPathComponent("savedata/1/CUSA00207/SPRJ0005/userdata0000")
            try Data("corrupt".utf8).write(to: payload)
            let before = try contents(b)
            XCTAssertThrowsError(try sync(b, root))
            XCTAssertEqual(try contents(b), before)
            try fm.removeItem(at: payload)
            try fm.createSymbolicLink(at: payload, withDestinationURL: a.user.appendingPathComponent("savedata/1/CUSA00207/SPRJ0005/userdata0000"))
            XCTAssertThrowsError(try sync(b, root))
            XCTAssertEqual(try contents(b), before)
        }
    }

    func testStaleChoiceAndLiveFolderDestinationsAreRejected() throws {
        try fixture { a, b, root in
            _ = try sync(a, root); try progress("B", b)
            let result = try sync(b, root)
            let old = try XCTUnwrap(result.conflicts.first { $0.files == (try? contents(a)) })
            try progress("changed remotely", a); _ = try sync(a, root)
            let before = try contents(b)
            XCTAssertThrowsError(try sync(b, root, choice: CloudSaveChoice(device: old.id, digest: old.digest)))
            XCTAssertEqual(try contents(b), before)
            XCTAssertThrowsError(try sync(b, b.user.appendingPathComponent("cloud")))
            XCTAssertThrowsError(try sync(b, b.directory.appendingPathComponent("cloud")))
        }
    }

    func testChoosingLocalConflictPublishesNewHeadAndRemotePreservesItsOldProgress() throws {
        try fixture { a, b, root in
            _ = try sync(a, root); _ = try sync(b, root)
            try progress("A offline", a); _ = try sync(a, root)
            let oldA = try contents(a)
            try progress("B offline", b)
            let local = try contents(b)
            let conflicts = try sync(b, root).conflicts
            let selected = try XCTUnwrap(conflicts.first { $0.files == local })
            let result = try sync(b, root, choice: CloudSaveChoice(device: selected.id, digest: selected.digest))
            XCTAssertFalse(result.imported)
            XCTAssertEqual(try contents(b), local)
            XCTAssertTrue(try b.backups().contains { record in
                guard let payload = try? b.verify(record), let files = try? SaveStore.inventory(payload.appendingPathComponent("savedata")).files else { return false }
                return record.label == "iCloud conflict backup" && files == oldA
            })
            XCTAssertTrue(try sync(a, root).imported)
            XCTAssertEqual(try contents(a), local)
            let displaced = try XCTUnwrap(try a.backups().first)
            XCTAssertEqual(try SaveStore.inventory(a.verify(displaced).appendingPathComponent("savedata")).files, oldA)
        }
    }

    func testPendingRestoreAndIncompleteNormalFilesDoNotPublish() throws {
        try fixture { a, _, root in
            try a.seedCheckpoint()
            try Data("broken journal".utf8).write(to: a.user.appendingPathComponent("quicksaves/CUSA00207/restore.json"))
            XCTAssertThrowsError(try sync(a, root))
            XCTAssertFalse(fm.fileExists(atPath: root.path))
            try fm.removeItem(at: a.user.appendingPathComponent("quicksaves/CUSA00207/restore.json"))
            try Data().write(to: a.user.appendingPathComponent("savedata/1/CUSA00207/SPRJ0005/userdata0000"))
            XCTAssertThrowsError(try sync(a, root))
            XCTAssertFalse((try? fm.contentsOfDirectory(atPath: root.path))?.contains(where: { $0.hasSuffix(".bbechoessave") }) == true)
        }
    }

    func testUntaggedCloudFolderIsNeverAdoptedAndDisabledDefaultsRemainOff() throws {
        try fixture { a, _, root in
            try fm.createDirectory(at: root, withIntermediateDirectories: true)
            try Data("other document".utf8).write(to: root.appendingPathComponent("other.txt"))
            let before = try SaveStore.inventory(root).files
            XCTAssertThrowsError(try sync(a, root))
            XCTAssertEqual(try SaveStore.inventory(root).files, before)
            XCTAssertFalse(LauncherPrefs().cloudSaves)
        }
    }

    /// Explicit opt-in integration check; never discovers or imports real saves.
    func testSelectedICloudDriveWithSyntheticPayloadOnly() throws {
        guard let parent = ProcessInfo.processInfo.environment["BLOOD_ECHOES_ICLOUD_QA"] else {
            throw XCTSkip("Set BLOOD_ECHOES_ICLOUD_QA to an existing iCloud Drive directory for the synthetic integration check.")
        }
        let parentURL = URL(fileURLWithPath: parent, isDirectory: true)
        let root = parentURL.appendingPathComponent("Blood-Echoes-Sync-QA-" + UUID().uuidString)
        defer { try? fm.removeItem(at: root) }
        try SaveStoreTests().fixture { store, _ in
            let before = try contents(store)
            _ = try sync(store, root)
            let package = try XCTUnwrap(try fm.contentsOfDirectory(at: root, includingPropertiesForKeys: nil).first { $0.pathExtension == "bbechoessave" })
            let manifest = package.appendingPathComponent("manifest.json")
            let uploadFiles = try XCTUnwrap(fm.enumerator(at: package, includingPropertiesForKeys: [.isRegularFileKey]))
                .allObjects.compactMap { $0 as? URL }.filter { (try? $0.resourceValues(forKeys: [.isRegularFileKey]).isRegularFile) == true }
            XCTAssertEqual(uploadFiles.count, 3) // manifest plus both required normal-save files
            XCTAssertTrue(fm.isUbiquitousItem(at: manifest), "The selected folder is not managed by iCloud Drive")
            let deadline = Date().addingTimeInterval(25)
            var uploaded = false
            repeat {
                uploaded = try uploadFiles.allSatisfy { file in
                    var fresh = file; fresh.removeAllCachedResourceValues()
                    return try fresh.resourceValues(forKeys: [.ubiquitousItemIsUploadedKey]).ubiquitousItemIsUploaded == true
                }
                if !uploaded { Thread.sleep(forTimeInterval: 0.5) }
            } while !uploaded && Date() < deadline
            if !uploaded {
                for var file in uploadFiles {
                    file.removeAllCachedResourceValues()
                    let status = try file.resourceValues(forKeys: [.isUbiquitousItemKey, .ubiquitousItemIsUploadedKey, .ubiquitousItemIsUploadingKey, .ubiquitousItemUploadingErrorKey])
                    print("Synthetic iCloud status", file.lastPathComponent, "managed", status.isUbiquitousItem as Any, "uploaded", status.ubiquitousItemIsUploaded as Any, "uploading", status.ubiquitousItemIsUploading as Any, "error", status.ubiquitousItemUploadingError as Any)
                }
            }
            XCTAssertTrue(uploaded, "iCloud did not confirm all synthetic files uploaded within the bounded window")
            XCTAssertTrue(try sync(store, root).conflicts.isEmpty)
            XCTAssertEqual(try contents(store), before)
            // Exercise replacement of this device's already uploaded package,
            // not just the first publication. The live save remains a fixture.
            try progress("synthetic progress after play", store)
            let updated = try contents(store)
            XCTAssertNotEqual(updated, before)
            _ = try sync(store, root)
            let replacementDeadline = Date().addingTimeInterval(25)
            repeat {
                uploaded = try uploadFiles.allSatisfy { file in
                    var fresh = file; fresh.removeAllCachedResourceValues()
                    return try fresh.resourceValues(forKeys: [.ubiquitousItemIsUploadedKey]).ubiquitousItemIsUploaded == true
                }
                if !uploaded { Thread.sleep(forTimeInterval: 0.5) }
            } while !uploaded && Date() < replacementDeadline
            XCTAssertTrue(uploaded, "iCloud did not confirm the replacement package uploaded")
            XCTAssertEqual(try SaveStore.normalInventory(package.appendingPathComponent("savedata")).files, updated)
            XCTAssertTrue(try sync(store, root).conflicts.isEmpty)
            XCTAssertEqual(try contents(store), updated)
            XCTAssertEqual(try fm.contentsOfDirectory(at: root, includingPropertiesForKeys: nil).filter { $0.pathExtension == "bbechoessave" }.count, 1)
        }
    }

    func testUploadAccountFailureIsVisibleWithoutChangingLocalProgress() throws {
        try fixture { a, _, root in
            let before = try contents(a)
            let cloud = CloudSaves(store: a, root: root, transferStatus: { _ in .failed("NSCocoaErrorDomain 4355: iCloud account unavailable") })
            let result = try a.locked { try cloud.synchronize() }
            XCTAssertTrue(result.message.contains("4355"))
            XCTAssertEqual(try contents(a), before)
            XCTAssertFalse(result.imported)
            XCTAssertTrue(result.conflicts.isEmpty)
            // A failed transfer still retains the verified local queued package.
            XCTAssertEqual(try fm.contentsOfDirectory(at: root, includingPropertiesForKeys: nil).filter { $0.pathExtension == "bbechoessave" }.count, 1)
        }
    }
}
