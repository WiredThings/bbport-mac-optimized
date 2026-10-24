// SPDX-License-Identifier: GPL-2.0-or-later
import Foundation
import CryptoKit
import Darwin

struct SaveBackup: Codable, Identifiable, Equatable, Sendable {
    let id: String
    let label: String
    let created: Date
    let kinds: [String]
    let files: [String: String]
    let bytes: Int64
    var kept: Bool? = nil
}

struct QuickCheckpoint: Identifiable, Sendable {
    let id: String
    let title: String
    let created: Date
    let current: Bool
    let bytes: Int64
    var kept = false
    var beforeRestore: Bool { id.hasPrefix("before-load-") }
    var identity: String { title + "/" + id }
}

struct SaveSummary: Sendable {
    let folder: URL
    let files: Int
    let bytes: Int64
    let modified: Date?
}

/// File checkpoints contain ordinary game saves; their manifest and active pointer are separate.
/// All changes require the same advisory lock as the running quicksave helper.
struct SaveStore: Sendable {
    let user: URL
    let directory: URL
    private var fm: FileManager { .default }
    private func fail(_ text: String) -> OnlineError { OnlineError(message: L10n.key(text)) }

    /// Metadata only: opening/refreshing the manager neither hashes every save nor
    /// creates checkpoints, directories, locks or mode markers.
    func currentGameSave() throws -> SaveSummary? {
        try summary(user.appendingPathComponent("savedata"))
    }

    private func summary(_ folder: URL) throws -> SaveSummary? {
        guard fm.fileExists(atPath: folder.path) else { return nil }
        let keys: Set<URLResourceKey> = [.isSymbolicLinkKey, .isRegularFileKey, .isDirectoryKey, .fileSizeKey, .contentModificationDateKey]
        for root in [user, folder] {
            guard try root.resourceValues(forKeys: keys).isSymbolicLink != true else { throw fail("Save folders must not contain symbolic links.") }
        }
        var enumerationError: Error?
        guard let entries = fm.enumerator(at: folder, includingPropertiesForKeys: Array(keys), errorHandler: { _, error in
            enumerationError = error; return false
        }) else { throw fail("Could not read the save folder.") }
        var count = 0, bytes: Int64 = 0, modified: Date?
        for case let path as URL in entries {
            let values = try path.resourceValues(forKeys: keys)
            guard values.isSymbolicLink != true else { throw fail("Save folders must not contain symbolic links.") }
            if values.isDirectory == true || path.lastPathComponent == ".DS_Store" || path.lastPathComponent.hasSuffix(".bbtmp") { continue }
            guard values.isRegularFile == true else { throw fail("Save files are incomplete or unsupported.") }
            count += 1; bytes += Int64(values.fileSize ?? 0)
            if let date = values.contentModificationDate, date > (modified ?? .distantPast) { modified = date }
        }
        if let error = enumerationError { throw error }
        return count == 0 ? nil : SaveSummary(folder: folder, files: count, bytes: bytes, modified: modified)
    }

    private func readSaveJSON(_ path: URL) throws -> [String: Any] {
        let values = try path.resourceValues(forKeys: [.isSymbolicLinkKey, .isRegularFileKey, .fileSizeKey])
        guard values.isSymbolicLink != true, values.isRegularFile == true,
              let size = values.fileSize, size <= 1024 * 1024,
              let object = try JSONSerialization.jsonObject(with: Data(contentsOf: path)) as? [String: Any]
        else { throw fail("Invalid save backup.") }
        return object
    }

    func checkpointFolder(_ checkpoint: QuickCheckpoint) throws -> URL {
        guard checkpoint.title.range(of: "^CUSA[0-9]{5}$", options: .regularExpression) != nil,
              checkpoint.id.range(of: "^(save-|before-load-)[0-9a-f]{32}$", options: .regularExpression) != nil else { throw fail("Invalid save backup.") }
        return user.appendingPathComponent("quicksaves/" + checkpoint.title + "/" + checkpoint.id)
    }

    func backupFolder(_ record: SaveBackup) throws -> URL {
        guard UUID(uuidString: record.id) != nil else { throw fail("Invalid save backup.") }
        return directory.appendingPathComponent(record.id)
    }

    func backupCurrentGameSave() throws {
        guard try currentGameSave() != nil else { throw fail("No game saves yet.") }
        _ = try backup(label: "Game save backup", kinds: ["savedata"])
    }

    /// Build an ordinary helper-compatible checkpoint without changing the active
    /// pointer. A pre-F3 backup is exported as a new selectable save checkpoint.
    private func withCheckpointPayload<T>(_ checkpoint: QuickCheckpoint, _ operation: (URL) throws -> T) throws -> T {
        let source = try verifiedCheckpoint(checkpoint)
        let before = try Self.inventory(source).files
        let temp = fm.temporaryDirectory.appendingPathComponent("blood-echoes-checkpoint-" + UUID().uuidString)
        defer { try? fm.removeItem(at: temp) }
        let title = temp.appendingPathComponent("quicksaves/" + checkpoint.title)
        let id = checkpoint.beforeRestore ? "save-" + String(checkpoint.id.dropFirst("before-load-".count)) : checkpoint.id
        let folder = title.appendingPathComponent(id)
        try fm.createDirectory(at: folder, withIntermediateDirectories: true)
        try fm.copyItem(at: source, to: folder.appendingPathComponent("data"))
        try fm.copyItem(at: try checkpointFolder(checkpoint).appendingPathComponent("manifest.json"), to: folder.appendingPathComponent("manifest.json"))
        try JSONSerialization.data(withJSONObject: ["checkpoint": id]).write(to: title.appendingPathComponent("current.json"), options: .atomic)
        guard try Self.inventory(source).files == before,
              try Self.inventory(folder.appendingPathComponent("data")).files == before else { throw fail("Save changed during backup; nothing was replaced.") }
        let copied = try readSaveJSON(folder.appendingPathComponent("manifest.json"))
        guard let records = copied["files"] as? [String: [String: Any]], records.count == before.count,
              before.allSatisfy({ records[$0.key]?["sha256"] as? String == $0.value }) else { throw fail("Invalid save backup.") }
        return try operation(temp)
    }

    func backupCheckpoint(_ checkpoint: QuickCheckpoint) throws {
        try withCheckpointPayload(checkpoint) { payload in
            _ = try backup(label: "Quick checkpoint backup", source: payload, kinds: ["quicksaves"])
        }
    }

    /// Publish a verified new directory; never merge into or overwrite an export.
    /// Refuse destinations inside live saves/archives, including symlink aliases.
    private func exportPayload(_ source: URL, to parent: URL, label: String) throws -> URL {
        let destination = parent.resolvingSymlinksInPath().standardizedFileURL
        guard try destination.resourceValues(forKeys: [.isDirectoryKey]).isDirectory == true else { throw fail("Choose an export folder") }
        for protected in [user, directory] {
            let path = protected.resolvingSymlinksInPath().standardizedFileURL.path
            guard destination.path != path, !destination.path.hasPrefix(path + "/") else { throw fail("Choose an export folder outside saves and backups.") }
        }
        let expected = try Self.inventory(source).files
        let id = UUID().uuidString
        let stage = destination.appendingPathComponent(".blood-echoes-export-" + id)
        let target = destination.appendingPathComponent("Blood-Echoes-" + label + "-" + id)
        defer { try? fm.removeItem(at: stage) }
        try fm.copyItem(at: source, to: stage)
        guard try Self.inventory(stage).files == expected, try Self.inventory(source).files == expected else {
            throw fail("Backup verification failed; current saves were not changed.")
        }
        try fm.moveItem(at: stage, to: target)
        return target
    }

    func exportCurrentGameSave(to parent: URL) throws -> URL {
        guard try currentGameSave() != nil else { throw fail("No game saves yet.") }
        // Export's savedata wrapper preserves the existing Import contract.
        let temp = fm.temporaryDirectory.appendingPathComponent("blood-echoes-save-" + UUID().uuidString)
        defer { try? fm.removeItem(at: temp) }
        try fm.createDirectory(at: temp, withIntermediateDirectories: true)
        let source = user.appendingPathComponent("savedata")
        let expected = try Self.inventory(source).files
        try fm.copyItem(at: source, to: temp.appendingPathComponent("savedata"))
        guard try Self.inventory(source).files == expected,
              try Self.inventory(temp.appendingPathComponent("savedata")).files == expected else { throw fail("Save changed during backup; nothing was replaced.") }
        return try exportPayload(temp, to: parent, label: "Game-Saves")
    }

    func exportCheckpoint(_ checkpoint: QuickCheckpoint, to parent: URL) throws -> URL {
        try withCheckpointPayload(checkpoint) { try exportPayload($0, to: parent, label: "Quick-Checkpoint") }
    }

    func exportBackup(_ record: SaveBackup, to parent: URL) throws -> URL {
        try exportPayload(verify(record), to: parent, label: "Backup")
    }

    func locked<T>(_ operation: () throws -> T) throws -> T {
        try fm.createDirectory(at: user, withIntermediateDirectories: true)
        guard try user.resourceValues(forKeys: [.isSymbolicLinkKey]).isSymbolicLink != true else { throw fail("Save folders must not contain symbolic links.") }
        let path = user.appendingPathComponent(".quicksave-session.lock").path
        let fd = Darwin.open(path, O_RDWR | O_CREAT | O_NOFOLLOW, 0o600)
        guard fd >= 0 else { throw fail("Could not lock the save folder.") }
        defer { Darwin.close(fd) }
        guard flock(fd, LOCK_EX | LOCK_NB) == 0 else { throw fail("Close the game before managing saves.") }
        defer { flock(fd, LOCK_UN) }
        return try operation()
    }

    static func inventory(_ root: URL) throws -> (files: [String: String], bytes: Int64) {
        let fm = FileManager.default
        let keys: Set<URLResourceKey> = [.isSymbolicLinkKey, .isRegularFileKey, .isDirectoryKey, .fileSizeKey]
        guard try root.resourceValues(forKeys: keys).isSymbolicLink != true else {
            throw OnlineError(message: L10n.key("Save folders must not contain symbolic links."))
        }
        let scan = root.resolvingSymlinksInPath().standardizedFileURL
        var files = [String: String](), bytes: Int64 = 0
        var enumerationError: Error?
        guard let entries = fm.enumerator(at: scan, includingPropertiesForKeys: Array(keys), errorHandler: { _, error in
            enumerationError = error; return false
        }) else { throw OnlineError(message: L10n.key("Could not read the save folder.")) }
        for case let path as URL in entries {
            let values = try path.resourceValues(forKeys: keys)
            guard values.isSymbolicLink != true else { throw OnlineError(message: L10n.key("Save folders must not contain symbolic links.")) }
            if values.isDirectory == true { continue }
            if path.lastPathComponent == ".DS_Store" { continue }
            guard values.isRegularFile == true, !path.lastPathComponent.hasSuffix(".bbtmp") else {
                throw OnlineError(message: L10n.key("Save files are incomplete or unsupported."))
            }
            let handle = try FileHandle(forReadingFrom: path)
            var hash = SHA256()
            do {
                while let part = try handle.read(upToCount: 1024 * 1024), !part.isEmpty { hash.update(data: part) }
                try handle.close()
            } catch { try? handle.close(); throw error }
            let relative = path.pathComponents.suffix(entries.level).joined(separator: "/")
            files[relative] = hash.finalize().map { String(format: "%02x", $0) }.joined()
            bytes += Int64(values.fileSize ?? 0)
        }
        if let error = enumerationError { throw error }
        return (files, bytes)
    }

    func backups() throws -> [SaveBackup] {
        guard fm.fileExists(atPath: directory.path) else { return [] }
        return try fm.contentsOfDirectory(at: directory, includingPropertiesForKeys: nil)
            .filter { UUID(uuidString: $0.lastPathComponent) != nil }
            .compactMap { folder -> SaveBackup? in
                guard var record = try? JSONDecoder().decode(SaveBackup.self, from: Data(contentsOf: folder.appendingPathComponent("backup.json"))) else { return nil }
                record.kept = fm.fileExists(atPath: folder.appendingPathComponent(".keep").path)
                return record
            }
            .sorted { $0.created > $1.created }
    }

    @discardableResult
    func backup(label: String, source: URL? = nil, kinds: [String] = ["savedata"]) throws -> SaveBackup {
        let source = source ?? user
        guard kinds.allSatisfy({ ["savedata", "quicksaves"].contains($0) }) else { throw fail("Invalid save backup.") }
        if Self.automaticBackupLabels.contains(label),
           let previous = try backups().first(where: { $0.label == label && Set($0.kinds) == Set(kinds) }),
           let payload = try? verify(previous),
           try backupContentKey(source, kinds: kinds) == backupContentKey(payload, kinds: kinds) {
            let refreshed = SaveBackup(id: previous.id, label: previous.label, created: Date(), kinds: previous.kinds, files: previous.files, bytes: previous.bytes, kept: previous.kept)
            try JSONEncoder().encode(refreshed).write(to: try backupFolder(previous).appendingPathComponent("backup.json"), options: .atomic)
            if previous.files.keys.contains(where: { $0.hasSuffix("/.keep") }) { try setKept(true, backup: refreshed) }
            return refreshed
        }
        try fm.createDirectory(at: directory, withIntermediateDirectories: true)
        let id = UUID().uuidString
        let stage = directory.appendingPathComponent(".backup-" + id)
        let payload = stage.appendingPathComponent("data")
        try fm.createDirectory(at: payload, withIntermediateDirectories: true)
        defer { try? fm.removeItem(at: stage) }
        var used = [String]()
        for kind in kinds where fm.fileExists(atPath: source.appendingPathComponent(kind).path) {
            let from = source.appendingPathComponent(kind)
            let before = try Self.inventory(from)
            try fm.copyItem(at: from, to: payload.appendingPathComponent(kind))
            guard try Self.inventory(payload.appendingPathComponent(kind)).files == before.files,
                  try Self.inventory(from).files == before.files else { throw fail("Save changed during backup; nothing was replaced.") }
            used.append(kind)
        }
        let inventory = try Self.inventory(payload)
        let record = SaveBackup(id: id, label: label, created: Date(), kinds: used, files: inventory.files, bytes: inventory.bytes)
        try JSONEncoder().encode(record).write(to: stage.appendingPathComponent("backup.json"), options: .atomic)
        if inventory.files.keys.contains(where: { $0.hasSuffix("/.keep") }) {
            try Data("Blood Echoes keep v1\n".utf8).write(to: stage.appendingPathComponent(".keep"), options: .atomic)
        }
        try fm.moveItem(at: stage, to: directory.appendingPathComponent(id))
        return record
    }

    func verify(_ record: SaveBackup) throws -> URL {
        guard UUID(uuidString: record.id) != nil else { throw fail("Invalid save backup.") }
        let folder = directory.appendingPathComponent(record.id)
        guard try folder.resourceValues(forKeys: [.isSymbolicLinkKey]).isSymbolicLink != true else { throw fail("Invalid save backup.") }
        let data = folder.appendingPathComponent("data")
        guard try Self.inventory(data).files == record.files else { throw fail("Backup verification failed; current saves were not changed.") }
        return data
    }

    /// Swap whole directories atomically on macOS, never merge character/profile files.
    private func install(_ source: URL, kind: String) throws {
        guard ["savedata", "quicksaves"].contains(kind) else { throw fail("Invalid save backup.") }
        try atomicInstall(source, live: user.appendingPathComponent(kind))
    }

    /// Cloud imports follow the same whole-save, offline-restore contract as UI
    /// restores. Conflict copies are manual/protected history, not auto retention.
    func restoreCloudNormal(_ source: URL, conflict: Bool) throws {
        _ = try Self.normalInventory(source)
        if try currentGameSave() != nil {
            _ = try backup(label: conflict ? "iCloud conflict backup" : "Before iCloud restore", kinds: ["savedata", "quicksaves"])
        }
        try markOfflineRestore()
        try install(source, kind: "savedata")
        try replaceCheckpointsWithCurrent()
    }

    static func normalInventory(_ source: URL) throws -> (files: [String: String], bytes: Int64) {
        let value = try inventory(source)
        let titles = Set(value.files.keys.compactMap { path -> String? in
            let parts = path.split(separator: "/")
            return parts.count > 3 && parts[0] == "1" && String(parts[1]).range(of: "^CUSA[0-9]{5}$", options: .regularExpression) != nil ? String(parts[1]) : nil
        })
        guard !titles.isEmpty, try titles.allSatisfy({ title in
            try ["userdata0000", "userdata0010"].allSatisfy {
                let path = "1/" + title + "/SPRJ0005/" + $0
                guard value.files[path] != nil else { return false }
                return (try source.appendingPathComponent(path).resourceValues(forKeys: [.fileSizeKey]).fileSize ?? 0) > 0
            }
        }) else { throw OnlineError(message: L10n.key("Save files are incomplete or unsupported.")) }
        return value
    }

    private func atomicInstall(_ source: URL, live: URL) throws {
        let expected = try Self.inventory(source).files
        try fm.createDirectory(at: live.deletingLastPathComponent(), withIntermediateDirectories: true)
        let stage = live.deletingLastPathComponent().appendingPathComponent(".save-restore-" + UUID().uuidString)
        defer { try? fm.removeItem(at: stage) }
        try fm.copyItem(at: source, to: stage)
        guard try Self.inventory(stage).files == expected else { throw fail("Backup verification failed; current saves were not changed.") }
        if fm.fileExists(atPath: live.path) {
            _ = try Self.inventory(live)
            guard renameatx_np(AT_FDCWD, stage.path, AT_FDCWD, live.path, UInt32(RENAME_SWAP)) == 0 else {
                throw fail("Could not replace saves; the original was preserved.")
            }
        } else { try fm.moveItem(at: stage, to: live) }
    }

    func checkpoints() throws -> [QuickCheckpoint] {
        let root = user.appendingPathComponent("quicksaves")
        guard fm.fileExists(atPath: root.path) else { return [] }
        guard try root.resourceValues(forKeys: [.isSymbolicLinkKey]).isSymbolicLink != true else { throw fail("Save folders must not contain symbolic links.") }
        var result = [QuickCheckpoint]()
        for title in try fm.contentsOfDirectory(at: root, includingPropertiesForKeys: nil)
            where title.lastPathComponent.range(of: "^CUSA[0-9]{5}$", options: .regularExpression) != nil {
            guard try title.resourceValues(forKeys: [.isSymbolicLinkKey]).isSymbolicLink != true else { throw fail("Save folders must not contain symbolic links.") }
            let pointerURL = title.appendingPathComponent("current.json")
            let pointer = fm.fileExists(atPath: pointerURL.path) ? try readSaveJSON(pointerURL)["checkpoint"] as? String : nil
            for folder in try fm.contentsOfDirectory(at: title, includingPropertiesForKeys: nil)
                where folder.lastPathComponent.range(of: "^(save-|before-load-)[0-9a-f]{32}$", options: .regularExpression) != nil {
                guard try folder.resourceValues(forKeys: [.isSymbolicLinkKey]).isSymbolicLink != true else { throw fail("Save folders must not contain symbolic links.") }
                let manifestURL = folder.appendingPathComponent("manifest.json")
                // F2 publishes its manifest and pointer only after copying data. An
                // unfinished unpublished directory is not a usable history entry.
                if pointer != folder.lastPathComponent && !fm.fileExists(atPath: manifestURL.path) { continue }
                let manifest = try readSaveJSON(manifestURL)
                guard let created = manifest["created"] as? Double, created.isFinite else { throw fail("Invalid save backup.") }
                guard let data = try summary(folder.appendingPathComponent("data")) else {
                    if pointer == folder.lastPathComponent { throw fail("Invalid save backup.") }
                    continue
                }
                result.append(QuickCheckpoint(id: folder.lastPathComponent, title: title.lastPathComponent,
                                              created: Date(timeIntervalSince1970: created),
                                              current: folder.lastPathComponent.hasPrefix("save-") && pointer == folder.lastPathComponent,
                                              bytes: data.bytes, kept: fm.fileExists(atPath: folder.appendingPathComponent(".keep").path)))
            }
        }
        return result.sorted { $0.created > $1.created }
    }

    var offlineRestoreMarker: URL { user.appendingPathComponent(".quickload-offline.json") }
    var hasOfflineRestoreIntent: Bool { fm.fileExists(atPath: offlineRestoreMarker.path) }

    private func validateOfflineRestoreIntent() throws {
        guard try offlineRestoreMarker.resourceValues(forKeys: [.isSymbolicLinkKey, .fileSizeKey]).isSymbolicLink != true,
              (try offlineRestoreMarker.resourceValues(forKeys: [.fileSizeKey]).fileSize ?? 1025) <= 1024,
              let value = try JSONSerialization.jsonObject(with: Data(contentsOf: offlineRestoreMarker)) as? [String: Int],
              value["version"] == 1 else { throw fail("Invalid save backup.") }
    }

    private func markOfflineRestore() throws {
        if hasOfflineRestoreIntent { try validateOfflineRestoreIntent() }
        try JSONSerialization.data(withJSONObject: ["version": 1]).write(to: offlineRestoreMarker, options: .atomic)
    }

    func verifiedCheckpoint(_ checkpoint: QuickCheckpoint) throws -> URL {
        guard try checkpoints().contains(where: { $0.id == checkpoint.id && $0.title == checkpoint.title && (!checkpoint.current || $0.current) }) else { throw fail("Saves changed. Refresh and try again.") }
        let folder = try checkpointFolder(checkpoint)
        let manifest = try readSaveJSON(folder.appendingPathComponent("manifest.json"))
        guard let records = manifest["files"] as? [String: [String: Any]] else { throw fail("Invalid save backup.") }
        let data = folder.appendingPathComponent("data")
        let actual = try Self.inventory(data).files
        guard actual.count == records.count, actual.allSatisfy({ records[$0.key]?["sha256"] as? String == $0.value }),
              actual["SPRJ0005/userdata0000"] != nil, actual["SPRJ0005/userdata0010"] != nil else {
            throw fail("Backup verification failed; current saves were not changed.")
        }
        return data
    }

    func restoreCheckpoint(_ checkpoint: QuickCheckpoint) throws {
        let data = try verifiedCheckpoint(checkpoint)
        try backup(label: "Before restore", kinds: ["savedata", "quicksaves"])
        try markOfflineRestore()
        try atomicInstall(data, live: user.appendingPathComponent("savedata/1/" + checkpoint.title))
        try seedCheckpoint()
    }

    func restore(_ record: SaveBackup, kind: String) throws {
        guard record.kinds.contains(kind) else { throw fail("This backup does not contain that save type.") }
        let payload = try verify(record)
        // Keep both normal saves and checkpoints, so returning offline cannot expose an old pointer.
        try backup(label: "Before restore", kinds: ["savedata", "quicksaves"])
        try markOfflineRestore()
        try install(payload.appendingPathComponent(kind), kind: kind)
        if kind == "savedata" { try replaceCheckpointsWithCurrent() }
    }

    func clearQuickload() throws {
        let quick = user.appendingPathComponent("quicksaves")
        if fm.fileExists(atPath: quick.path) {
            _ = try Self.inventory(quick)
            try fm.removeItem(at: quick) // caller has verified a durable backup first
        }
    }

    /// A checkpoint of the committed on-disk save, before the next game launch.
    func seedCheckpoint(into destination: URL? = nil) throws {
        let savedata = user.appendingPathComponent("savedata/1")
        guard fm.fileExists(atPath: savedata.path) else { return }
        for title in try fm.contentsOfDirectory(at: savedata, includingPropertiesForKeys: nil) {
            let files = try Self.inventory(title).files
            guard files["SPRJ0005/userdata0000"] != nil, files["SPRJ0005/userdata0010"] != nil else { continue }
            guard title.lastPathComponent.range(of: "^CUSA[0-9]{5}$", options: .regularExpression) != nil else { continue }
            let root = (destination ?? user.appendingPathComponent("quicksaves")).appendingPathComponent(title.lastPathComponent)
            let id = "save-" + UUID().uuidString.replacingOccurrences(of: "-", with: "").lowercased()
            let folder = root.appendingPathComponent(id)
            try fm.createDirectory(at: folder, withIntermediateDirectories: true)
            var published = false
            defer { if !published { try? fm.removeItem(at: folder) } }
            try fm.copyItem(at: title, to: folder.appendingPathComponent("data"))
            guard try Self.inventory(folder.appendingPathComponent("data")).files == files else { throw fail("Could not create the checkpoint.") }
            var records = [String: [String: Any]]()
            for (path, hash) in files {
                records[path] = ["sha256": hash, "size": try title.appendingPathComponent(path).resourceValues(forKeys: [.fileSizeKey]).fileSize ?? 0]
            }
            try JSONSerialization.data(withJSONObject: ["files": records, "created": Date().timeIntervalSince1970]).write(to: folder.appendingPathComponent("manifest.json"), options: .atomic)
            try JSONSerialization.data(withJSONObject: ["checkpoint": id]).write(to: root.appendingPathComponent("current.json"), options: .atomic)
            published = true
        }
    }

    /// Prepare every new checkpoint before replacing the old active tree. A failed
    /// copy leaves all existing pointers in place; the caller already made a backup.
    private func replaceCheckpointsWithCurrent() throws {
        let stage = user.appendingPathComponent(".checkpoint-seed-" + UUID().uuidString)
        defer { try? fm.removeItem(at: stage) }
        try fm.createDirectory(at: stage, withIntermediateDirectories: true)
        try seedCheckpoint(into: stage)
        try install(stage, kind: "quicksaves")
    }

    /// Preserve the existing helper's crash-recovery contract before clearing any pointers.
    func recoverInterruptedQuickload() throws {
        let quick = user.appendingPathComponent("quicksaves")
        guard fm.fileExists(atPath: quick.path) else { return }
        guard try quick.resourceValues(forKeys: [.isSymbolicLinkKey]).isSymbolicLink != true else { throw fail("Invalid save backup.") }
        for title in try fm.contentsOfDirectory(at: quick, includingPropertiesForKeys: nil) {
            let journal = title.appendingPathComponent("restore.json")
            guard fm.fileExists(atPath: journal.path) else { continue }
            guard title.lastPathComponent.range(of: "^CUSA[0-9]{5}$", options: .regularExpression) != nil,
                  try title.resourceValues(forKeys: [.isSymbolicLinkKey]).isSymbolicLink != true,
                  try journal.resourceValues(forKeys: [.isSymbolicLinkKey]).isSymbolicLink != true,
                  let data = try JSONSerialization.jsonObject(with: Data(contentsOf: journal)) as? [String: Any],
                  let backup = data["backup"] as? String,
                  backup.range(of: "^before-load-[0-9a-f]{32}$", options: .regularExpression) != nil,
                  let before = data["before"] as? [String: [String: Any]],
                  let after = data["after"] as? [String: [String: Any]] else { throw fail("Invalid save backup.") }
            func matches(_ url: URL, _ expected: [String: [String: Any]]) throws -> Bool {
                let files = try Self.inventory(url).files
                return files.count == expected.count && files.allSatisfy { expected[$0.key]?["sha256"] as? String == $0.value }
            }
            let live = user.appendingPathComponent("savedata/1/" + title.lastPathComponent)
            if !fm.fileExists(atPath: live.path) {
                let original = title.appendingPathComponent(backup).appendingPathComponent("data")
                guard try matches(original, before) else { throw fail("Backup verification failed; current saves were not changed.") }
                try atomicInstall(original, live: live)
            } else {
                let validBefore = try matches(live, before)
                let validAfter = try matches(live, after)
                guard validBefore || validAfter else { throw fail("Save recovery needs attention; backups were preserved.") }
            }
            try fm.removeItem(at: journal)
        }
    }

    func prepareMode(online: Bool, legacy: URL? = nil, newSession: Bool = false) throws {
        try locked {
            try recoverInterruptedQuickload()
            if let legacy, fm.fileExists(atPath: legacy.appendingPathComponent("savedata").path),
               !fm.fileExists(atPath: directory.appendingPathComponent("legacy-migrated.json").path) {
                guard try legacy.resourceValues(forKeys: [.isSymbolicLinkKey]).isSymbolicLink != true,
                      try String(contentsOf: legacy.appendingPathComponent(".blood-echoes-online-save")) == "Blood Echoes isolated online saves v1\n" else { throw fail("Invalid save backup.") }
                try backup(label: "Before shared saves", kinds: ["savedata", "quicksaves"])
                let copy = try backup(label: "Legacy online progress", source: legacy)
                if online { try install(try verify(copy).appendingPathComponent("savedata"), kind: "savedata") }
                try Data(copy.id.utf8).write(to: directory.appendingPathComponent("legacy-migrated.json"), options: .atomic)
            }
            let state = directory.appendingPathComponent("mode.json")
            let previous = (try? JSONDecoder().decode(Bool.self, from: Data(contentsOf: state)))
            let restored = hasOfflineRestoreIntent
            if restored { try validateOfflineRestoreIntent() }
            let quick = user.appendingPathComponent("quicksaves")
            if online && (previous != true || newSession || restored || !fm.fileExists(atPath: quick.path)) {
                try backup(label: "Before online: quicksave archive", kinds: ["savedata", "quicksaves"])
                try replaceCheckpointsWithCurrent()
            } else if !online && previous == true && !restored {
                try backup(label: "Returned offline", kinds: ["savedata", "quicksaves"])
                try replaceCheckpointsWithCurrent()
            }
            try fm.createDirectory(at: directory, withIntermediateDirectories: true)
            try JSONEncoder().encode(online).write(to: state, options: .atomic)
            // Keep the guard across offline launches; only an explicit online entry clears it.
            if restored && online { try fm.removeItem(at: offlineRestoreMarker) }
        }
    }
}
