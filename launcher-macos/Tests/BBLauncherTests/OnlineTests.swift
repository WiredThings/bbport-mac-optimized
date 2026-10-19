import XCTest
@testable import BBLauncher

final class OnlineTests: XCTestCase {
    func testOfflineDoesNotRetainInheritedCredentialsOrChangeGraphics() throws {
        var prefs = LauncherPrefs()
        prefs.quicksave = true
        prefs.language = "11"; prefs.communityChinese = true
        prefs.chineseSimplifiedFolder = "/managed/zh-cn"
        var game = GameSettings(); game.upscaler = "metalfx"; game.preset = 1
        let off = OptimizedProfile(prefs: prefs, game: game)
        let environment = off.applyingEnvironment(to: ["BB_THD_TOKEN":"secret", "BB_NET_MODULE":"old",
            "BB_SHADNET_PASSWORD":"secret", "SHADPS4_HTTP_HOST_OVERRIDES_JSON":"old"])
        XCTAssertEqual(environment["BB_ONLINE"], "0")
        XCTAssertNil(environment["BB_THD_TOKEN"])
        XCTAssertNil(environment["BB_NET_MODULE"])
        XCTAssertNil(environment["SHADPS4_HTTP_HOST_OVERRIDES_JSON"])
        prefs.online = true
        let on = OptimizedProfile(prefs: prefs, game: game)
        XCTAssertEqual(on.environment["BB_ONLINE"], "1")
        XCTAssertEqual(on.applyingEnvironment(to: ["BB_SKIP_NETWORK_CHOICE":"1"])["BB_SKIP_NETWORK_CHOICE"], "0")
        XCTAssertEqual(on.environment["BB_QUICKSAVE"], "0")
        XCTAssertEqual(off.environment["BB_QUICKSAVE"], "1")
        XCTAssertTrue(prefs.quicksave)
        XCTAssertEqual(on.settings, off.settings)
        for key in off.environment.keys where key.hasPrefix("BB_MTL_") || key.hasPrefix("BB_METAL_") || key.hasPrefix("BB_CHINESE_") {
            XCTAssertEqual(on.environment[key], off.environment[key], key)
        }
    }

    func testOnlinePreferenceDefaultsOffAndPersistsWithoutAlteringOthers() throws {
        let suite = "bbport.online.tests.\(UUID().uuidString)"
        let defaults = try XCTUnwrap(UserDefaults(suiteName: suite))
        defer { defaults.removePersistentDomain(forName: suite) }
        var prefs = LauncherPrefs.load(from: defaults)
        XCTAssertFalse(prefs.online)
        prefs.online = true; prefs.save(to: defaults)
        XCTAssertEqual(LauncherPrefs.load(from: defaults), prefs)
        prefs.renderer = "vulkan"
        XCTAssertFalse(OptimizedProfile(prefs: prefs, game: GameSettings()).onlineActive)
    }

    func testSaveIsolationCopiesOnceAndNeverOverwritesEitherSide() throws {
        let root = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: root) }
        let formal = root.appendingPathComponent("formal")
        let target = root.appendingPathComponent("online")
        try FileManager.default.createDirectory(at: formal.appendingPathComponent("savedata/1"), withIntermediateDirectories: true)
        let original = formal.appendingPathComponent("savedata/1/save")
        try Data("formal".utf8).write(to: original)
        try OnlineSaves.prepare(source: formal, destination: target)
        let copy = target.appendingPathComponent("savedata/1/save")
        XCTAssertEqual(try Data(contentsOf: copy), Data("formal".utf8))
        try Data("online progress".utf8).write(to: copy)
        try OnlineSaves.prepare(source: formal, destination: target)
        XCTAssertEqual(try Data(contentsOf: copy), Data("online progress".utf8))
        XCTAssertEqual(try Data(contentsOf: original), Data("formal".utf8))
        XCTAssertFalse(FileManager.default.fileExists(atPath: target.appendingPathComponent("quicksaves").path))
    }

    func testSaveIsolationRejectsLinksAndUnmarkedDestination() throws {
        let root = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: root) }
        let source = root.appendingPathComponent("source")
        let destination = root.appendingPathComponent("online")
        try FileManager.default.createDirectory(at: source.appendingPathComponent("savedata"), withIntermediateDirectories: true)
        try FileManager.default.createSymbolicLink(atPath: source.appendingPathComponent("savedata/link").path,
                                                   withDestinationPath: "/tmp")
        XCTAssertThrowsError(try OnlineSaves.prepare(source: source, destination: destination))
        XCTAssertFalse(FileManager.default.fileExists(atPath: destination.path))
        try FileManager.default.createDirectory(at: destination, withIntermediateDirectories: true)
        XCTAssertThrowsError(try OnlineSaves.prepare(source: source, destination: destination))
    }

    func testVerificationURLCannotSendLoginToAnotherHost() {
        XCTAssertNotNil(HuntersDream.trustedVerificationURL("https://thehuntersdream.com/link"))
        for url in ["http://thehuntersdream.com/link", "https://thehuntersdream.com.evil.test/link",
                    "https://evil.test/link", "https://user@thehuntersdream.com/link", "https://thehuntersdream.com:123/link"] {
            XCTAssertNil(HuntersDream.trustedVerificationURL(url))
        }
        XCTAssertNotEqual(OnlineAccountStore(bundleIdentifier: "production").service,
                          OnlineAccountStore(bundleIdentifier: "qa").service)
    }

    func testExistingOnlineMarkerMustNotBeALink() throws {
        let root = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: root) }
        let destination = root.appendingPathComponent("online")
        try FileManager.default.createDirectory(at: destination, withIntermediateDirectories: true)
        let original = root.appendingPathComponent("marker")
        try "Blood Echoes isolated online saves v1\n".write(to: original, atomically: true, encoding: .utf8)
        try FileManager.default.createSymbolicLink(at: destination.appendingPathComponent(".blood-echoes-online-save"),
                                                   withDestinationURL: original)
        XCTAssertThrowsError(try OnlineSaves.prepare(source: root, destination: destination))
    }
}
