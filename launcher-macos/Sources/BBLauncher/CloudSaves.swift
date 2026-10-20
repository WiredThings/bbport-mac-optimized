// SPDX-License-Identifier: GPL-2.0-or-later
import Foundation
import CryptoKit
import Darwin

struct CloudSaveVersion: Codable, Identifiable, Sendable {
    let version: Int
    let id: String
    let deviceName: String
    let created: Date
    let clock: [String: Int]
    let files: [String: String]
    let bytes: Int64

    var digest: String { CloudSaves.digest(files) }
    var identity: String { id + "/" + digest }
}

struct CloudSaveChoice: Sendable {
    let device: String
    let digest: String
}

struct CloudSaveResult: Sendable {
    var message: String
    var conflicts: [CloudSaveVersion] = []
    var imported = false
}

enum CloudTransferState: Sendable {
    case local, queued, uploaded, failed(String)
}

private struct CloudSaveState: Codable {
    var device = UUID().uuidString
    var root: String
    var clock: [String: Int] = [:]
    var digest: String?
}

/// File-based iCloud Drive interoperability without a private CloudKit container.
/// Each installation owns one latest normal-save package. Vector clocks preserve
/// concurrent offline progress; wall-clock timestamps never pick a winner.
struct CloudSaves: Sendable {
    let store: SaveStore
    let root: URL
    var transferStatus: @Sendable (URL) throws -> CloudTransferState = { try CloudSaves.systemTransferStatus($0) }
    private var fm: FileManager { .default }
    private let marker = "Blood Echoes normal save sync v1\n"

    static func systemTransferStatus(_ package: URL) throws -> CloudTransferState {
        guard let entries = FileManager.default.enumerator(at: package, includingPropertiesForKeys: nil) else { return .queued }
        var cloudFiles = 0, pending = false
        for case var file as URL in entries {
            file.removeAllCachedResourceValues()
            let value = try file.resourceValues(forKeys: [.isRegularFileKey, .isUbiquitousItemKey, .ubiquitousItemIsUploadedKey, .ubiquitousItemUploadingErrorKey, .ubiquitousItemDownloadingErrorKey])
            guard value.isRegularFile == true else { continue }
            guard value.isUbiquitousItem == true else { pending = true; continue }
            cloudFiles += 1
            if let error = value.ubiquitousItemUploadingError ?? value.ubiquitousItemDownloadingError { return .failed(error.localizedDescription) }
            if value.ubiquitousItemIsUploaded != true { pending = true }
        }
        return cloudFiles == 0 ? .local : pending ? .queued : .uploaded
    }

    static func digest(_ files: [String: String]) -> String {
        let data = try! JSONSerialization.data(withJSONObject: files, options: [.sortedKeys])
        return SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined()
    }

    static func dominates(_ a: [String: Int], _ b: [String: Int]) -> Bool {
        b.allSatisfy { a[$0.key, default: 0] >= $0.value } && a.contains { $0.value > b[$0.key, default: 0] }
    }

    private func fail(_ message: String) -> OnlineError { OnlineError(message: L10n.key(message)) }

    private func coordinated<T>(_ url: URL, writing: Bool = false, _ body: (URL) throws -> T) throws -> T {
        var error: NSError?
        var result: Result<T, Error>?
        let coordinator = NSFileCoordinator(filePresenter: nil)
        if writing {
            coordinator.coordinate(writingItemAt: url, options: .forReplacing, error: &error) { coordinatedURL in result = Result { try body(coordinatedURL) } }
        } else {
            coordinator.coordinate(readingItemAt: url, options: .withoutChanges, error: &error) { coordinatedURL in result = Result { try body(coordinatedURL) } }
        }
        if let error { throw error }
        guard let result else { throw fail("iCloud Drive is not ready. Try again later.") }
        return try result.get()
    }

    private func ready(_ path: URL) throws {
        let value = try path.resourceValues(forKeys: [.isSymbolicLinkKey, .isDirectoryKey, .isUbiquitousItemKey, .ubiquitousItemDownloadingStatusKey, .ubiquitousItemHasUnresolvedConflictsKey])
        guard value.isSymbolicLink != true else { throw fail("Save folders must not contain symbolic links.") }
        guard value.ubiquitousItemHasUnresolvedConflicts != true else { throw fail("iCloud Drive has an unresolved file conflict. Both saves were preserved.") }
        if value.isUbiquitousItem == true, value.isDirectory != true, value.ubiquitousItemDownloadingStatus != .current {
            try fm.startDownloadingUbiquitousItem(at: path)
            throw fail("iCloud save is downloading. Try again after it finishes.")
        }
    }

    private func prepareRoot() throws {
        let canonical = root.resolvingSymlinksInPath().standardizedFileURL
        for protected in [store.user, store.directory] {
            let path = protected.resolvingSymlinksInPath().standardizedFileURL.path
            guard canonical.path != path, !canonical.path.hasPrefix(path + "/"), !path.hasPrefix(canonical.path + "/") else {
                throw fail("Choose an iCloud folder outside local saves and backups.")
            }
        }
        let parent = root.deletingLastPathComponent()
        guard fm.fileExists(atPath: parent.path) else { throw fail("iCloud Drive is not ready. Try again later.") }
        try ready(parent)
        try coordinated(root, writing: true) { url in
            if fm.fileExists(atPath: url.path) {
                try ready(url)
                let tag = url.appendingPathComponent("format.txt")
                guard fm.fileExists(atPath: tag.path) else { throw fail("The selected sync folder is not a Blood Echoes save folder.") }
                try ready(tag)
                guard (try tag.resourceValues(forKeys: [.fileSizeKey]).fileSize ?? Int.max) <= 1024,
                      try Data(contentsOf: tag) == Data(marker.utf8) else { throw fail("The selected sync folder is not a Blood Echoes save folder.") }
            } else {
                try fm.createDirectory(at: url, withIntermediateDirectories: false)
                try Data(marker.utf8).write(to: url.appendingPathComponent("format.txt"), options: .atomic)
            }
        }
    }

    private func package(_ device: String) -> URL { root.appendingPathComponent(device + ".bbechoessave") }

    private func read(_ url: URL) throws -> CloudSaveVersion {
        try coordinated(url) { folder in
            try ready(folder)
            let manifest = folder.appendingPathComponent("manifest.json")
            try ready(manifest)
            guard (try manifest.resourceValues(forKeys: [.fileSizeKey]).fileSize ?? Int.max) <= 1024 * 1024 else { throw fail("Invalid iCloud save package.") }
            let head = try JSONDecoder().decode(CloudSaveVersion.self, from: Data(contentsOf: manifest))
            guard head.version == 1, UUID(uuidString: head.id) != nil, url.lastPathComponent == head.id + ".bbechoessave",
                  head.deviceName.count <= 256, head.created.timeIntervalSince1970.isFinite,
                  head.clock.count <= 64, head.clock[head.id, default: 0] > 0,
                  head.clock.allSatisfy({ UUID(uuidString: $0.key) != nil && $0.value > 0 && $0.value < 1_000_000_000 }),
                  head.bytes > 0, head.bytes <= 256 * 1024 * 1024, head.files.count <= 10000 else { throw fail("Invalid iCloud save package.") }
            let data = folder.appendingPathComponent("savedata")
            try ready(data)
            guard let entries = fm.enumerator(at: data, includingPropertiesForKeys: nil) else { throw fail("Invalid iCloud save package.") }
            for case let file as URL in entries { try ready(file) }
            let inventory = try SaveStore.normalInventory(data)
            guard inventory.files == head.files, inventory.bytes == head.bytes else { throw fail("iCloud save is incomplete or changed. Local saves were preserved.") }
            return head
        }
    }

    /// Requires store.locked. No cloud I/O occurs while the game holds its session lock.
    func synchronize(choice: CloudSaveChoice? = nil) throws -> CloudSaveResult {
        try store.recoverInterruptedQuickload()
        try prepareRoot()
        let stateURL = store.directory.appendingPathComponent("cloud-state.json")
        var state: CloudSaveState
        if fm.fileExists(atPath: stateURL.path) {
            try ready(stateURL)
            guard (try stateURL.resourceValues(forKeys: [.fileSizeKey]).fileSize ?? Int.max) <= 65536 else { throw fail("Invalid iCloud save state.") }
            state = try JSONDecoder().decode(CloudSaveState.self, from: Data(contentsOf: stateURL))
            guard UUID(uuidString: state.device) != nil, state.clock.count <= 64,
                  state.clock.allSatisfy({ UUID(uuidString: $0.key) != nil && $0.value > 0 && $0.value < 1_000_000_000 }) else { throw fail("Invalid iCloud save state.") }
            if state.root != root.standardizedFileURL.path { state = CloudSaveState(root: root.standardizedFileURL.path) }
        } else { state = CloudSaveState(root: root.standardizedFileURL.path) }
        let urls = try fm.contentsOfDirectory(at: root, includingPropertiesForKeys: nil).filter { $0.pathExtension == "bbechoessave" }
        guard urls.count <= 64 else { throw fail("Too many iCloud save devices. Local saves were preserved.") }
        let heads = try urls.map { try read($0) }
        let live = store.user.appendingPathComponent("savedata")
        var files: [String: String]?
        if try store.currentGameSave() != nil { files = try SaveStore.normalInventory(live).files }
        let localDigest = files.map(Self.digest)
        let changed = localDigest != state.digest
        var localClock = state.clock
        let ownRevision = max(state.clock[state.device, default: 0], heads.first(where: { $0.id == state.device })?.clock[state.device, default: 0] ?? 0)
        if changed && files != nil { localClock[state.device] = ownRevision + 1 }
        var candidates = heads
        if let files {
            candidates.append(CloudSaveVersion(version: 1, id: state.device, deviceName: L10n.key("This Mac (current saves)"), created: Date(), clock: localClock, files: files, bytes: try SaveStore.normalInventory(live).bytes))
        }
        let frontier = candidates.filter { item in !candidates.contains { Self.dominates($0.clock, item.clock) } }
        let conflicts = Set(frontier.map(\.digest)).count > 1
        if conflicts && choice == nil {
            // No writes to any save payload or pointer on conflict.
            try fm.createDirectory(at: store.directory, withIntermediateDirectories: true)
            try JSONEncoder().encode(state).write(to: stateURL, options: .atomic)
            let unique = Dictionary(frontier.map { ($0.identity, $0) }, uniquingKeysWith: { _, last in last })
            return CloudSaveResult(message: L10n.key("Different save progress found. Choose which progress to continue."), conflicts: Array(unique.values).sorted { $0.created > $1.created })
        }
        var winner = frontier.first
        if let choice {
            guard let selected = candidates.first(where: { $0.id == choice.device && $0.digest == choice.digest }) else { throw fail("Saves changed. Refresh and try again.") }
            winner = selected
        }
        guard let winner else { return CloudSaveResult(message: L10n.key("No game saves yet.")) }
        if conflicts && choice != nil {
            // Preserve losing remote payloads locally too, including a stale
            // head owned by this device after an interrupted state-file write.
            var copied = Set<String>()
            for head in heads where head.digest != winner.digest && copied.insert(head.digest).inserted {
                try coordinated(package(head.id)) { folder in
                    let latest = try read(folder)
                    guard latest.files == head.files, latest.clock == head.clock else { throw fail("Saves changed. Refresh and try again.") }
                    let expected = Dictionary(uniqueKeysWithValues: head.files.map { ("savedata/" + $0.key, $0.value) })
                    let existing = try store.backups().first { $0.label == "iCloud conflict backup" && $0.files == expected }
                    if let existing { _ = try store.verify(existing) }
                    else { _ = try store.backup(label: "iCloud conflict backup", source: folder, kinds: ["savedata"]) }
                }
            }
        }
        // Merely observing equal contents does not create a new causal edit.
        // Otherwise two Macs would endlessly acknowledge each other's copies.
        var merged = state.clock
        for head in heads { for (device, revision) in head.clock { merged[device] = max(merged[device, default: 0], revision) } }
        var imported = false
        if winner.digest != localDigest {
            // Re-read under coordination before making the durable local backup.
            try coordinated(package(winner.id)) { folder in
                let latest = try read(folder)
                guard latest.files == winner.files, latest.clock == winner.clock else { throw fail("Saves changed. Refresh and try again.") }
                try store.restoreCloudNormal(folder.appendingPathComponent("savedata"), conflict: conflicts)
            }
            files = winner.files; imported = true
        }
        guard let files else { throw fail("Invalid iCloud save package.") }
        let publish = choice != nil || !heads.contains(where: { $0.files == files })
        if publish {
            merged[state.device] = max(merged[state.device, default: 0], ownRevision) + 1
            guard merged.count <= 64, merged.values.allSatisfy({ $0 < 1_000_000_000 }) else { throw fail("Invalid iCloud save state.") }
            let target = package(state.device)
            try coordinated(target, writing: true) { url in
                let stage = root.appendingPathComponent(".upload-" + UUID().uuidString)
                defer { try? fm.removeItem(at: stage) }
                try fm.createDirectory(at: stage, withIntermediateDirectories: false)
                try fm.copyItem(at: live, to: stage.appendingPathComponent("savedata"))
                let inventory = try SaveStore.normalInventory(stage.appendingPathComponent("savedata"))
                guard inventory.files == files, inventory.bytes <= 256 * 1024 * 1024, files.count <= 10000,
                      try SaveStore.normalInventory(live).files == files else { throw fail("Save changed during backup; nothing was replaced.") }
                let record = CloudSaveVersion(version: 1, id: state.device, deviceName: String((Host.current().localizedName ?? "Mac").prefix(256)), created: Date(), clock: merged, files: files, bytes: inventory.bytes)
                try JSONEncoder().encode(record).write(to: stage.appendingPathComponent("manifest.json"), options: .atomic)
                if fm.fileExists(atPath: url.path) {
                    try ready(url)
                    guard renameatx_np(AT_FDCWD, stage.path, AT_FDCWD, url.path, UInt32(RENAME_SWAP)) == 0 else { throw fail("Could not update iCloud saves. The previous copy was preserved.") }
                } else { try fm.moveItem(at: stage, to: url) }
            }
        }
        state.clock = merged; state.digest = Self.digest(files)
        try fm.createDirectory(at: store.directory, withIntermediateDirectories: true)
        try JSONEncoder().encode(state).write(to: stateURL, options: .atomic)
        let observedPackage = publish ? package(state.device) : package(heads.first(where: { $0.files == files })?.id ?? winner.id)
        let transfer: CloudTransferState
        do { transfer = try transferStatus(observedPackage) }
        catch { transfer = .failed(error.localizedDescription) }
        let message: String
        switch transfer {
        case .failed(let problem): message = L10n.format("iCloud transfer paused: %@", problem)
        case .uploaded: message = L10n.key("macOS confirmed the save files uploaded.")
        case .queued: message = L10n.key("Saved to iCloud Drive. macOS handles uploading.")
        case .local: message = L10n.key("iCloud Drive has not confirmed these files. Local copies are preserved.")
        }
        return CloudSaveResult(message: message, imported: imported)
    }
}
