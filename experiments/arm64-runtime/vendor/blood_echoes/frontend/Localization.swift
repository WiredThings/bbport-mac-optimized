// SPDX-License-Identifier: GPL-2.0-or-later
import Foundation

/// Launcher language is independent of the PS4 system language sent to the game.
/// English source strings are stable lookup keys; missing translations fall back to English.
enum L10n {
    static let languages: [Choice<String>] = [
        Choice(value: "system", label: "Follow system"),
        Choice(value: "en", label: "English"),
        Choice(value: "zh-Hans", label: "简体中文"),
        Choice(value: "zh-Hant", label: "繁體中文"),
    ]

    static func resolvedLanguage(_ preference: String, preferred: [String] = Locale.preferredLanguages) -> String {
        if ["en", "zh-Hans", "zh-Hant"].contains(preference) { return preference }
        for language in preferred {
            let code = language.lowercased().replacingOccurrences(of: "_", with: "-")
            if code.hasPrefix("zh") {
                return code.contains("hant") || code.hasSuffix("-tw") || code.hasSuffix("-hk") || code.hasSuffix("-mo")
                    ? "zh-Hant" : "zh-Hans"
            }
            if code.hasPrefix("en") { return "en" }
        }
        return "en"
    }

    static var language: String {
        resolvedLanguage(UserDefaults.standard.string(forKey: "uiLanguage") ?? "system")
    }
    static var locale: Locale { Locale(identifier: language) }

    private static let resources: Bundle = {
        // SwiftPM's generated accessor differs between native and Swift Build. Prefer the
        // app's embedded bundle so a relocated release never depends on the developer's path.
        if let url = Bundle.main.resourceURL?.appendingPathComponent("BBLauncher_BBLauncher.bundle"),
           let bundle = Bundle(url: url) { return bundle }
        return Bundle.module
    }()

    private static let bundles: [String: Bundle] = {
        // SwiftPM lowercases lproj directory names. Enumerate actual names rather than letting
        // Bundle's language fallback silently select Simplified Chinese for Traditional Chinese.
        guard let root = resources.resourceURL,
              let folders = try? FileManager.default.contentsOfDirectory(at: root, includingPropertiesForKeys: nil)
        else { return [:] }
        return Dictionary(uniqueKeysWithValues: folders.compactMap { url in
            guard url.pathExtension == "lproj", let bundle = Bundle(url: url) else { return nil }
            return (url.deletingPathExtension().lastPathComponent.lowercased(), bundle)
        })
    }()

    static func bundle(for language: String) -> Bundle {
        bundles[language.lowercased()] ?? bundles["en"] ?? resources
    }

    static func key(_ key: String, language: String? = nil) -> String {
        bundle(for: language ?? self.language).localizedString(forKey: key, value: key, table: nil)
    }

    /// Interpolations remain separate from the translation key and may be reordered by translators.
    static func format(_ key: String, _ arguments: String..., language: String? = nil) -> String {
        String(format: self.key(key, language: language), locale: Locale(identifier: language ?? self.language),
               arguments: arguments.map { $0 as CVarArg })
    }
}
