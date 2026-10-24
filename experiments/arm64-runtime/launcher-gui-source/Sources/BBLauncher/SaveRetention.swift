// SPDX-License-Identifier: GPL-2.0-or-later
import Foundation
import CryptoKit

struct SaveCleanup: Sendable {
    var items = 0
    var bytes: Int64 = 0
}

extension SaveStore {
    static let automaticBackupLabels: Set<String> = [
        "Before online: quicksave archive", "Returned offline", "Before restore", "Before iCloud restore"
    ]

    /// Only explicit automatic labels are eligible. Legacy migration, imports,
    /// unrecognised records and all manual backups remain indefinitely.
    static func automatic(_ record: SaveBackup) -> Bool { automaticBackupLabels.contains(record.label) }

    func setKept(_ keep: Bool, checkpoint: QuickCheckpoint) throws {
        _ = try verifiedCheckpoint(checkpoint)
        try setKeepMarker(keep, in: checkpointFolder(checkpoint))
    }

    func setKept(_ keep: Bool, backup: SaveBackup) throws {
        _ = try verify(backup)
        try setKeepMarker(keep, in: backupFolder(backup))
    }

    private func setKeepMarker(_ keep: Bool, in folder: URL) throws {
        let marker = folder.appendingPathComponent(".keep")
        if keep { try Data("Blood Echoes keep v1\n".utf8).write(to: marker, options: .atomic) }
        else if FileManager.default.fileExists(atPath: marker.path) { try FileManager.default.removeItem(at: marker) }
    }

    /// Stable save contents, ignoring checkpoint UUIDs and creation timestamps.
    /// Keeps the selected checkpoint and the set of historical contents distinct.
    func backupContentKey(_ source: URL, kinds: [String]) throws -> String {
        let fm = FileManager.default
        var values = [String: String]()
        func digest(_ files: [String: String]) throws -> String {
            let data = try JSONSerialization.data(withJSONObject: files, options: [.sortedKeys])
            return SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined()
        }
        for kind in kinds where fm.fileExists(atPath: source.appendingPathComponent(kind).path) {
            let root = source.appendingPathComponent(kind)
            let all = try Self.inventory(root).files // reject unsafe or unfinished trees first
            if kind != "quicksaves" {
                for (path, hash) in all { values[kind + "/" + path] = hash }
                continue
            }
            var consumed = Set<String>()
            for title in try fm.contentsOfDirectory(at: root, includingPropertiesForKeys: nil)
                where title.lastPathComponent.range(of: "^CUSA[0-9]{5}$", options: .regularExpression) != nil {
                let pointer = title.appendingPathComponent("current.json")
                let selected = (try? JSONSerialization.jsonObject(with: Data(contentsOf: pointer)) as? [String: String])?["checkpoint"]
                if selected != nil { consumed.insert(title.lastPathComponent + "/current.json") }
                for folder in try fm.contentsOfDirectory(at: title, includingPropertiesForKeys: nil)
                    where folder.lastPathComponent.range(of: "^(save-|before-load-)[0-9a-f]{32}$", options: .regularExpression) != nil {
                    let prefix = title.lastPathComponent + "/" + folder.lastPathComponent + "/"
                    let payload = all.filter { $0.key.hasPrefix(prefix + "data/") }
                    let relative = Dictionary(uniqueKeysWithValues: payload.map { (String($0.key.dropFirst((prefix + "data/").count)), $0.value) })
                    guard !relative.isEmpty, all[prefix + "manifest.json"] != nil else {
                        throw OnlineError(message: L10n.key("Invalid save backup."))
                    }
                    let key = try digest(relative)
                    let group = folder.lastPathComponent.hasPrefix("before-load-") ? "before-load" : "checkpoint"
                    values["quicksaves/" + title.lastPathComponent + "/" + group + "/" + key] = key
                    if folder.lastPathComponent == selected { values["quicksaves/" + title.lastPathComponent + "/active"] = key }
                    // Pinning is content policy, not incidental metadata.
                    if let flag = all[prefix + ".keep"] { values[prefix + ".keep"] = flag }
                    consumed.formUnion(payload.keys)
                    consumed.insert(prefix + "manifest.json")
                    consumed.insert(prefix + ".keep")
                }
            }
            for (path, hash) in all where !consumed.contains(path) { values[kind + "/" + path] = hash }
        }
        return try digest(values)
    }

    /// Caller holds the exclusive session lock. Never called by F2/F3 during play.
    /// All survivors and deletion candidates are verified before any deletion.
    func pruneHistory(dryRun: Bool = false) throws -> SaveCleanup {
        let fm = FileManager.default
        let quick = user.appendingPathComponent("quicksaves")
        if fm.fileExists(atPath: quick.path), let enumerator = fm.enumerator(at: quick, includingPropertiesForKeys: nil) {
            for case let path as URL in enumerator where path.lastPathComponent == "restore.json" {
                throw OnlineError(message: L10n.key("Save recovery needs attention; backups were preserved."))
            }
        }
        let normal = user.appendingPathComponent("savedata")
        guard fm.fileExists(atPath: normal.path), !(try Self.normalInventory(normal).files).isEmpty else { return SaveCleanup() }
        let checkpoints = try checkpoints()
        for active in checkpoints where active.current { _ = try verifiedCheckpoint(active) }
        var candidates = [URL]()
        for title in Set(checkpoints.map(\.title)) {
            guard !checkpoints.contains(where: { $0.title == title && !$0.beforeRestore }) || checkpoints.contains(where: { $0.title == title && $0.current }) else {
                throw OnlineError(message: L10n.key("Invalid save backup."))
            }
            for before in [false, true] {
                let old = checkpoints.filter { $0.title == title && !$0.current && !$0.kept && $0.beforeRestore == before }
                for checkpoint in old { _ = try verifiedCheckpoint(checkpoint) }
                candidates += try old.dropFirst(3).map { try checkpointFolder($0) }
            }
        }
        let archives = try backups().filter { Self.automatic($0) && $0.kept != true }
        for record in archives { _ = try verify(record) }
        candidates += try archives.dropFirst(5).map { try backupFolder($0) }
        var result = SaveCleanup()
        for folder in candidates {
            if fm.fileExists(atPath: folder.appendingPathComponent(".keep").path) { continue }
            // An archived interrupted transaction must remain recoverable too.
            if let entries = fm.enumerator(at: folder, includingPropertiesForKeys: nil),
               entries.allObjects.compactMap({ $0 as? URL }).contains(where: { $0.lastPathComponent == "restore.json" }) { continue }
            let size = try Self.inventory(folder).bytes
            if !dryRun { try fm.removeItem(at: folder) }
            result.items += 1; result.bytes += size
        }
        return result
    }
}
