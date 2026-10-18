// SPDX-License-Identifier: GPL-2.0-or-later
import XCTest
@testable import BBLauncher

final class LocalizationTests: XCTestCase {
    func testCommunityChineseRoutingPersistenceAndStaleEnvironment() throws {
        let suite = "bbport.community.tests.\(UUID().uuidString)"
        let defaults = try XCTUnwrap(UserDefaults(suiteName: suite))
        defer { defaults.removePersistentDomain(forName: suite) }
        XCTAssertFalse(LauncherPrefs.load(from: defaults).communityChinese)
        var prefs = LauncherPrefs()
        prefs.communityChinese = true
        prefs.chineseSimplifiedFolder = "/managed/simplified"
        prefs.chineseTraditionalFolder = "/managed/traditional"
        for language in ["10", "11", "1", "0", "18"] {
            prefs.language = language
            prefs.save(to: defaults)
            let saved = LauncherPrefs.load(from: defaults)
            XCTAssertEqual(saved, prefs)
            let chinese = ["10", "11"].contains(language)
            XCTAssertEqual(saved.usesCommunityChinese, chinese)
            let profile = OptimizedProfile(prefs: saved, game: GameSettings())
            let env = profile.applyingEnvironment(to: ["BB_CHINESE_SOURCE": "community",
                "BB_CHINESE_LANGUAGE": "stale", "BB_CHINESE_TRANSLATION_DIR": "/stale"])
            XCTAssertEqual(env["BB_LANGUAGE"], chinese ? "1" : language)
            XCTAssertEqual(env["BB_CHINESE_SOURCE"], chinese ? "community" : "official")
            XCTAssertEqual(env["BB_CHINESE_LANGUAGE"], chinese ? language : nil)
            XCTAssertEqual(env["BB_CHINESE_TRANSLATION_DIR"], chinese ? saved.chineseTranslationFolder : nil)
        }
        prefs.language = "11"
        prefs.communityChinese = false
        let env = OptimizedProfile(prefs: prefs, game: GameSettings()).environment
        XCTAssertEqual(env["BB_LANGUAGE"], "11")
        XCTAssertEqual(env["BB_CHINESE_SOURCE"], "official")
        XCTAssertNil(env["BB_CHINESE_TRANSLATION_DIR"])
    }

    func testChineseGameLanguagesPersistAndReachRuntimeIndependentlyOfUI() throws {
        let suite = "bbport.languages.tests.\(UUID().uuidString)"
        let defaults = try XCTUnwrap(UserDefaults(suiteName: suite))
        defer { defaults.removePersistentDomain(forName: suite) }
        for language in ["10", "11"] {
            var prefs = LauncherPrefs()
            prefs.gameFolder = "/games/CUSA03173-1.09"
            prefs.uiLanguage = "en"
            prefs.language = language
            prefs.save(to: defaults)
            let saved = LauncherPrefs.load(from: defaults)
            XCTAssertEqual(saved, prefs)
            XCTAssertEqual(OptimizedProfile(prefs: saved, game: GameSettings()).environment["BB_LANGUAGE"], language)
            prefs.uiLanguage = "zh-Hant"
            XCTAssertEqual(OptimizedProfile(prefs: prefs, game: GameSettings()).environment["BB_LANGUAGE"], language)
        }
        XCTAssertEqual(Catalog.languages.first(where: { $0.value == "11" })?.label, "简体中文")
        XCTAssertEqual(Catalog.languages.first(where: { $0.value == "10" })?.label, "繁體中文")
        XCTAssertEqual(Set(Catalog.languages.map(\.value)).count, Catalog.languages.count)
    }

    func testExistingPreferencesMigrateWithoutResettingGameChoices() throws {
        let suite = "bbport.languages.legacy.\(UUID().uuidString)"
        let defaults = try XCTUnwrap(UserDefaults(suiteName: suite))
        defer { defaults.removePersistentDomain(forName: suite) }
        defaults.set("8", forKey: "language")
        defaults.set("/games/current", forKey: "gameFolder")
        let saved = LauncherPrefs.load(from: defaults)
        XCTAssertEqual(saved.language, "8")
        XCTAssertEqual(saved.gameFolder, "/games/current")
        XCTAssertEqual(saved.uiLanguage, "system")
        defaults.set("unknown", forKey: "uiLanguage")
        XCTAssertEqual(LauncherPrefs.load(from: defaults).uiLanguage, "system")
    }

    func testSystemChineseScriptAndRegionResolution() {
        for code in ["zh-CN", "zh-SG", "zh-Hans", "zh"] {
            XCTAssertEqual(L10n.resolvedLanguage("system", preferred: [code]), "zh-Hans")
        }
        for code in ["zh-TW", "zh-HK", "zh-MO", "zh-Hant-HK"] {
            XCTAssertEqual(L10n.resolvedLanguage("system", preferred: [code]), "zh-Hant")
        }
        XCTAssertEqual(L10n.resolvedLanguage("en", preferred: ["zh-CN"]), "en")
        XCTAssertEqual(L10n.resolvedLanguage("system", preferred: ["fr-FR"]), "en")
        XCTAssertEqual(L10n.resolvedLanguage("system", preferred: ["en-GB", "zh-CN"]), "en")
    }

    func testTranslationResourcesAndFormatArguments() throws {
        func table(_ code: String) throws -> [String: String] {
            let url = try XCTUnwrap(L10n.bundle(for: code).url(forResource: "Localizable", withExtension: "strings"))
            return try XCTUnwrap(PropertyListSerialization.propertyList(from: Data(contentsOf: url), format: nil) as? [String: String])
        }
        let english = try table("en")
        XCTAssertGreaterThan(english.count, 250)
        for code in ["zh-Hans", "zh-Hant"] {
            let translated = try table(code)
            XCTAssertEqual(Set(translated.keys), Set(english.keys))
            for (key, value) in translated {
                XCTAssertFalse(value.isEmpty, key)
                XCTAssertEqual(key.components(separatedBy: "%@").count, value.components(separatedBy: "%@").count, key)
            }
        }
        XCTAssertEqual(L10n.key("Launcher language", language: "zh-Hans"), "启动器语言")
        XCTAssertEqual(L10n.key("Launcher language", language: "zh-Hant"), "啟動器語言")
        XCTAssertEqual(L10n.format("Version %@", "1.09", language: "zh-Hans"), "版本 1.09")
        XCTAssertEqual(L10n.key("Untranslated diagnostic", language: "zh-Hans"), "Untranslated diagnostic")
    }

    func testMissingChineseAssetsWarnWithoutTouchingGameFiles() throws {
        let root = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: root) }
        for (language, code) in [("11", "zhocn"), ("10", "zhotw")] {
            XCTAssertNotNil(gameLanguageWarning(folder: root.path, language: language))
            for path in ["msg/\(code)/menu.msgbnd.dcx", "msg/\(code)/item.msgbnd.dcx", "menu/\(code)/font.gfx"] {
                let url = root.appendingPathComponent("dvdroot_ps4/" + path)
                try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
                try Data([1, 2, 3]).write(to: url)
            }
            XCTAssertNil(gameLanguageWarning(folder: root.path, language: language))
        }
    }
}
