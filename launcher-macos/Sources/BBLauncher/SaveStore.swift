// SPDX-License-Identifier: GPL-2.0-or-later
import Foundation
import CryptoKit
import Darwin

struct SaveBackup: Codable, Identifiable, Equatable {
    let id: String
    let label: String
    let created: Date
    let kinds: [String]
    let files: [String: String]
    let bytes: Int64
}

struct QuickCheckpoint: Identifiable {
    let id: String
    let title: String
    let created: Date
    let current: Bool
}

/// File checkpoints contain ordinary game saves; their manifest and active pointer are separate.
/// All changes require the same advisory lock as the running quicksave helper.
struct SaveStore {
    let user: URL
    let directory: URL
    private let fm = FileManager.default
    private func fail(_ text: String) -> OnlineError { OnlineError(message: L10n.key(text)) }

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
            .compactMap { try? JSONDecoder().decode(SaveBackup.self, from: Data(contentsOf: $0.appendingPathComponent("backup.json"))) }
            .sorted { $0.created > $1.created }
    }

    @discardableResult
    func backup(label: String, source: URL? = nil, kinds: [String] = ["savedata"]) throws -> SaveBackup {
        let source = source ?? user
        guard kinds.allSatisfy({ ["savedata", "quicksaves"].contains($0) }) else { throw fail("Invalid save backup.") }
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
            let pointer = (try? JSONSerialization.jsonObject(with: Data(contentsOf: title.appendingPathComponent("current.json")))) as? [String: String]
            for folder in try fm.contentsOfDirectory(at: title, includingPropertiesForKeys: nil)
                where folder.lastPathComponent.range(of: "^save-[0-9a-f]{32}$", options: .regularExpression) != nil {
                guard try folder.resourceValues(forKeys: [.isSymbolicLinkKey]).isSymbolicLink != true else { throw fail("Save folders must not contain symbolic links.") }
                guard let manifest = (try? JSONSerialization.jsonObject(with: Data(contentsOf: folder.appendingPathComponent("manifest.json")))) as? [String: Any],
                      let created = manifest["created"] as? Double else { continue }
                result.append(QuickCheckpoint(id: folder.lastPathComponent, title: title.lastPathComponent,
                                              created: Date(timeIntervalSince1970: created), current: pointer?["checkpoint"] == folder.lastPathComponent))
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

    func restoreCheckpoint(_ checkpoint: QuickCheckpoint) throws {
        guard try checkpoints().contains(where: { $0.id == checkpoint.id && $0.title == checkpoint.title }) else { throw fail("Invalid save backup.") }
        let folder = user.appendingPathComponent("quicksaves/" + checkpoint.title + "/" + checkpoint.id)
        let manifest = try JSONSerialization.jsonObject(with: Data(contentsOf: folder.appendingPathComponent("manifest.json"))) as? [String: Any]
        guard let records = manifest?["files"] as? [String: [String: Any]] else { throw fail("Invalid save backup.") }
        let data = folder.appendingPathComponent("data")
        let actual = try Self.inventory(data).files
        guard actual.count == records.count, actual.allSatisfy({ records[$0.key]?["sha256"] as? String == $0.value }),
              actual["SPRJ0005/userdata0000"] != nil, actual["SPRJ0005/userdata0010"] != nil else {
            throw fail("Backup verification failed; current saves were not changed.")
        }
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
    private func recoverInterruptedQuickload() throws {
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
