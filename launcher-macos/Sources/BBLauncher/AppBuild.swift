// SPDX-License-Identifier: GPL-2.0-or-later
import AppKit
import Foundation

struct AppBuild {
    let version: String
    let revision: String?

    init(info: [String: Any], manifest: Data? = nil) {
        version = info["CFBundleShortVersionString"] as? String ?? ""
        func valid(_ value: Any?) -> String? {
            guard let text = value as? String,
                  text.range(of: "^(?:[0-9a-fA-F]{40}|[0-9a-fA-F]{64})$", options: .regularExpression) != nil else { return nil }
            return text.lowercased()
        }
        var fallback: String?
        if let manifest, let value = try? JSONSerialization.jsonObject(with: manifest) {
            if let object = value as? [String: Any] { fallback = valid(object["source_commit"]) }
            else if let records = value as? [[String: Any]], let root = records.first(where: { $0["component"] as? String == "blood-echoes" }) {
                fallback = valid(root["revision"])
            }
        }
        revision = valid(info["BBSourceCommit"]) ?? fallback
    }

    var shortRevision: String? { revision.map { String($0.prefix(7)) } }
    var displayVersion: String { shortRevision.map { "\(version) (\($0))" } ?? version }
    var aboutOptions: [NSApplication.AboutPanelOptionKey: Any] {
        // Suppress the numeric CFBundleVersion fallback when provenance is unknown.
        [.applicationVersion: version, .version: shortRevision ?? ""]
    }
    static var current: AppBuild {
        let manifest = Bundle.main.resourceURL?.appendingPathComponent("bbport/SOURCE-MANIFEST.json")
        return AppBuild(info: Bundle.main.infoDictionary ?? [:], manifest: manifest.flatMap { try? Data(contentsOf: $0) })
    }
}
