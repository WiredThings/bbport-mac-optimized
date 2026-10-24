// SPDX-License-Identifier: GPL-2.0-or-later
import Foundation

struct ChineseTranslationStatus: Decodable {
    var folder = ""
    var valid = false
}

/// Copies only GOTY text/font assets to application data; no Nexus credentials or assets ship.
struct ChineseTranslationHelper {
    let repo: URL
    let python: String
    let data: URL

    func run(_ action: String, folder: String, language: String) throws -> ChineseTranslationStatus {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: python)
        process.arguments = [repo.appendingPathComponent("scripts/chinese_translation.py").path,
                             action, "--folder", folder, "--language", language,
                             "--data", data.appendingPathComponent("chinese-translations").path]
        var env = ProcessInfo.processInfo.environment
        env["PYTHONDONTWRITEBYTECODE"] = "1"
        env["PYTHONNOUSERSITE"] = "1"
        env.removeValue(forKey: "PYTHONPATH")
        env.removeValue(forKey: "PYTHONHOME")
        process.environment = env
        let pipe = Pipe()
        process.standardOutput = pipe
        process.standardError = FileHandle.nullDevice
        try process.run()
        let output = pipe.fileHandleForReading.readDataToEndOfFile()
        process.waitUntilExit()
        guard process.terminationStatus == 0 else {
            let report = (try? JSONSerialization.jsonObject(with: output)) as? [String: String]
            throw NSError(domain: "Chinese Translation", code: Int(process.terminationStatus), userInfo: [
                NSLocalizedDescriptionKey: report?["error"] ?? L10n.key("Could not import or verify the translation package.")])
        }
        return try JSONDecoder().decode(ChineseTranslationStatus.self, from: output)
    }
}
