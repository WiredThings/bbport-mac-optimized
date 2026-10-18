import XCTest
@testable import BBLauncher

final class MetalOptimizationTests: XCTestCase {
    func testDefaultsMatchAcceptedProfileAndExperimentsStayOff() {
        XCTAssertEqual(MetalOptimizations.all.count, 25)
        XCTAssertEqual(Set(MetalOptimizations.all.map(\.id)).count, 25)
        XCTAssertEqual(MetalOptimizations.adopted.count, 20)
        XCTAssertEqual(MetalOptimizations.optionalExperiments.count, 5)
        let prefs = LauncherPrefs()
        var game = GameSettings(); game.upscaler = "metalfx"
        let env = OptimizedProfile(prefs: prefs, game: game).environment
        for option in MetalOptimizations.all {
            XCTAssertEqual(env[option.id], option.defaultOn ? "1" : "0", option.id)
            XCTAssertEqual(option.defaultOn, [.adopted, .adoptedExperimental, .bundle].contains(option.evidence))
            for parent in option.requires { XCTAssertTrue(MetalOptimizations.all.contains { $0.id == parent }) }
        }
        XCTAssertEqual(env["BB_MTL_PERSIST_WARM_ARCHIVE"], "0")
        XCTAssertFalse(MetalOptimizations.all.contains { $0.id.contains("OFFLINE") })
    }

    func testEveryVisibleToggleReachesTheProfileEnvironment() {
        for option in MetalOptimizations.all {
            var prefs = LauncherPrefs()
            prefs.metalOptimizations[option.id] = !option.defaultOn
            var game = GameSettings(); game.upscaler = "metalfx"
            let env = OptimizedProfile(prefs: prefs, game: game).environment
            XCTAssertEqual(env[option.id], option.defaultOn ? "0" : "1", option.id)
        }
    }

    func testDependenciesDisableRuntimeFlagWithoutLosingPreference() {
        for option in MetalOptimizations.all where !option.requires.isEmpty {
            for parent in option.requires {
                var prefs = LauncherPrefs()
                prefs.metalOptimizations[option.id] = true
                prefs.metalOptimizations[parent] = false
                XCTAssertEqual(MetalOptimizations.environment(prefs: prefs)[option.id], "0")
                XCTAssertTrue(prefs.metalOptimizations[option.id]!)
                prefs.metalOptimizations[parent] = true
                XCTAssertEqual(MetalOptimizations.environment(prefs: prefs)[option.id], "1")
            }
        }
    }

    func testPersistenceAndResetDoNotChangeQualityOrReimportOldOverrides() throws {
        let name = "bbport.metal-options.\(UUID().uuidString)"
        let defaults = try XCTUnwrap(UserDefaults(suiteName: name))
        defer { defaults.removePersistentDomain(forName: name) }
        var prefs = LauncherPrefs()
        prefs.extraEnv = "BB_MTL_DIRECT_COPY=0"
        prefs.resolution = "2560x1440"
        prefs.metalOptimizations = MetalOptimizations.defaults.mapValues { !$0 }
        prefs.save(to: defaults)
        XCTAssertEqual(LauncherPrefs.load(from: defaults), prefs)
        var game = GameSettings(); game.upscaler = "metalfx"; game.preset = 2
        let quality = OptimizedProfile(prefs: prefs, game: game).settings
        prefs.metalOptimizations = MetalOptimizations.defaults
        prefs.save(to: defaults)
        let restored = LauncherPrefs.load(from: defaults)
        XCTAssertEqual(restored.resolution, "2560x1440")
        XCTAssertEqual(restored.metalOptimizations["BB_MTL_DIRECT_COPY"], true)
        XCTAssertEqual(OptimizedProfile(prefs: restored, game: game).settings, quality)
    }

    func testLegacyDeveloperOverridesMigrateOnceAndControlsWin() throws {
        let name = "bbport.metal-options.legacy.\(UUID().uuidString)"
        let defaults = try XCTUnwrap(UserDefaults(suiteName: name))
        defer { defaults.removePersistentDomain(forName: name) }
        defaults.set("BB_MTL_DIRECT_COPY=0 BB_MTL_INLINE_UPDATES=1 BB_OTHER=1", forKey: "extraEnv")
        var prefs = LauncherPrefs.load(from: defaults)
        XCTAssertEqual(prefs.metalOptimizations, ["BB_MTL_DIRECT_COPY":false, "BB_MTL_INLINE_UPDATES":true])
        prefs.metalOptimizations["BB_MTL_DIRECT_COPY"] = true
        prefs.save(to: defaults)
        prefs = LauncherPrefs.load(from: defaults)
        let profile = OptimizedProfile(prefs: prefs, game: GameSettings())
        let env = profile.applyingEnvironment(to: ["BB_MTL_DIRECT_COPY":"0", "BB_OTHER":"1"])
        XCTAssertEqual(env["BB_MTL_DIRECT_COPY"], "1")
        XCTAssertEqual(env["BB_OTHER"], "1")
        prefs.renderer = "vulkan"
        let vk = OptimizedProfile(prefs: prefs, game: GameSettings()).applyingEnvironment(to: env)
        for option in MetalOptimizations.all { XCTAssertNil(vk[option.id]) }
        XCTAssertFalse(vk.keys.contains { $0.hasPrefix("BB_MTL_") })
    }

    func testRelaxedModeIsExplicitAndCanOverrideOldStrictDebugFlag() {
        var prefs = LauncherPrefs()
        prefs.metalOptimizations["BB_MTL_RELAXED_MATH"] = true
        let env = OptimizedProfile(prefs: prefs, game: GameSettings()).applyingEnvironment(to: ["BB_MTL_STRICT_FLOAT":"1"])
        XCTAssertEqual(env["BB_MTL_RELAXED_MATH"], "1")
        XCTAssertEqual(env["BB_MTL_STRICT_FLOAT"], "0")
    }

    func testOldDiagnosticSelectorsCannotOverrideVisibleControls() {
        let prefs = LauncherPrefs()
        let profile = OptimizedProfile(prefs: prefs, game: GameSettings())
        let env = profile.applyingEnvironment(to: ["BB_MTL_PROXY_CLEAR_FILE":"/tmp/old-ab-control",
                                                   "BB_MTL_FULL_USAGE":"1"])
        XCTAssertNil(env["BB_MTL_PROXY_CLEAR_FILE"])
        XCTAssertEqual(env["BB_MTL_FULL_USAGE"], "0")
        XCTAssertEqual(env["BB_MTL_PROXY_CLEAR"], "1")
    }

    func testMetalFXDependencyPreservesStoredChoiceAndQuality() {
        var prefs = LauncherPrefs()
        prefs.metalOptimizations["BB_MTL_UPSCALE_ALPHA"] = true
        var game = GameSettings(); game.upscaler = "off"
        XCTAssertEqual(OptimizedProfile(prefs: prefs, game: game).environment["BB_MTL_UPSCALE_ALPHA"], "0")
        XCTAssertEqual(prefs.metalOptimizations["BB_MTL_UPSCALE_ALPHA"], true)
        game.upscaler = "metalfx"
        XCTAssertEqual(OptimizedProfile(prefs: prefs, game: game).environment["BB_MTL_UPSCALE_ALPHA"], "1")
        prefs.ultrawide = true
        XCTAssertEqual(OptimizedProfile(prefs: prefs, game: game).environment["BB_MTL_UPSCALE_ALPHA"], "0")
    }

    func testNewReclaimerImportsLegacyOverrideEvenWithOlderControlDictionary() throws {
        let name = "bbport.reclaim-migration.\(UUID().uuidString)"
        let defaults = try XCTUnwrap(UserDefaults(suiteName: name))
        defer { defaults.removePersistentDomain(forName: name) }
        defaults.set(["BB_MTL_DIRECT_COPY": true], forKey: "metalOptimizations")
        defaults.set("BB_TEXTURE_INVALID_RECLAIM=0 BB_MTL_DIRECT_COPY=0", forKey: "extraEnv")
        var prefs = LauncherPrefs.load(from: defaults)
        XCTAssertEqual(prefs.metalOptimizations["BB_TEXTURE_INVALID_RECLAIM"], false)
        XCTAssertEqual(prefs.metalOptimizations["BB_MTL_DIRECT_COPY"], true)
        XCTAssertEqual(OptimizedProfile(prefs: prefs, game: GameSettings()).environment["BB_TEXTURE_INVALID_RECLAIM"], "0")
        prefs.metalOptimizations = MetalOptimizations.defaults
        prefs.save(to: defaults)
        XCTAssertEqual(LauncherPrefs.load(from: defaults).metalOptimizations["BB_TEXTURE_INVALID_RECLAIM"], true)
    }

    func testDiagnosticsAndUnsupportedBackendFlagsCannotOutliveControls() {
        var prefs = LauncherPrefs()
        let stale = ["BB_MTL_GPU_TIMING": "1", "BB_GPU_PROFILE": "1", "BB_PREUPLOAD": "1", "BB_VK_VALIDATION": "1"]
        var env = OptimizedProfile(prefs: prefs, game: GameSettings()).applyingEnvironment(to: stale)
        for key in stale.keys { XCTAssertNil(env[key], key) }
        prefs.gpuProfile = true
        env = OptimizedProfile(prefs: prefs, game: GameSettings()).applyingEnvironment(to: stale)
        XCTAssertEqual(env["BB_MTL_GPU_TIMING"], "1")
        XCTAssertNil(env["BB_GPU_PROFILE"])
        prefs.renderer = "vulkan"
        env = OptimizedProfile(prefs: prefs, game: GameSettings()).applyingEnvironment(to: stale)
        XCTAssertEqual(env["BB_GPU_PROFILE"], "1")
        XCTAssertNil(env["BB_MTL_GPU_TIMING"])
    }

    func testEveryOptionHasEnglishAndChineseDescriptions() {
        for language in ["en", "zh-Hans", "zh-Hant"] {
            let bundle = L10n.bundle(for: language)
            for option in MetalOptimizations.all {
                for key in [option.title, option.note, option.evidence.rawValue] {
                    XCTAssertNotEqual(bundle.localizedString(forKey: key, value: "missing", table: nil), "missing", key)
                }
            }
        }
    }
}
