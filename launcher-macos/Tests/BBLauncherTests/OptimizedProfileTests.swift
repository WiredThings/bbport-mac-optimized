import XCTest
@testable import BBLauncher

final class OptimizedProfileTests: XCTestCase {

    func testMetalOcclusionToggleAndBackendIsolation() {
        let suite = "bbport.occlusion.tests.\(UUID().uuidString)"
        let defaults = UserDefaults(suiteName: suite)!
        defer { defaults.removePersistentDomain(forName: suite) }
        var prefs = LauncherPrefs.load(from: defaults)
        XCTAssertTrue(prefs.metalOcclusion)
        XCTAssertEqual(OptimizedProfile(prefs: prefs, game: GameSettings()).environment["BB_METAL_OCCLUSION"], "1")
        prefs.metalOcclusion = false
        prefs.save(to: defaults)
        XCTAssertFalse(LauncherPrefs.load(from: defaults).metalOcclusion)
        prefs.metalOcclusion = true
        prefs.renderer = "vulkan"
        XCTAssertEqual(OptimizedProfile(prefs: prefs, game: GameSettings()).environment["BB_METAL_OCCLUSION"], "0")
    }
    func testNativeWidePatchAndReturnToStandardResolution() {
        var prefs = LauncherPrefs()
        prefs.renderer = "vulkan"
        prefs.ultrawide = true
        prefs.resolution = "2560x1440"
        var game = GameSettings()
        game.upscaler = "fsr3"
        game.preset = 3
        var profile = OptimizedProfile(prefs: prefs, game: game)
        XCTAssertEqual(profile.settings.outputRes, "1920x1080")
        XCTAssertEqual(profile.settings.upscaler, "off")
        XCTAssertEqual(profile.environment["BB_PATCHES"], "Resolution Patch 3840x1080 (32:9)")
        XCTAssertNil(profile.environment["BB_DISPLAY_ASPECT"])
        XCTAssertEqual(profile.environment["BB_DMEM_MB"], "9152")
        XCTAssertNil(profile.environment["BB_RENDER_RES"])
        prefs.ultrawide = false
        profile = OptimizedProfile(prefs: prefs, game: game)
        XCTAssertEqual(profile.settings.outputRes, "2560x1440")
        XCTAssertEqual(profile.settings.upscaler, "fsr3")
        XCTAssertEqual(profile.environment["BB_PATCHES"], "")
        XCTAssertNil(profile.environment["BB_DISPLAY_ASPECT"])
        XCTAssertNil(profile.environment["BB_DMEM_MB"])
    }

    func testMetalDefaultsAndUnsupportedEffectsAreNormalized() {
        var prefs = LauncherPrefs()
        XCTAssertEqual(prefs.renderer, "metal")
        prefs.language = "11"
        prefs.quicksave = true
        var game = GameSettings()
        game.upscaler = "fsr3"
        game.preset = 1
        game.objectMotion = true
        game.sharpen = true
        let profile = OptimizedProfile(prefs: prefs, game: game)
        XCTAssertEqual(profile.settings.upscaler, "metalfx")
        XCTAssertEqual(profile.settings.preset, 1)
        XCTAssertFalse(profile.settings.objectMotion)
        XCTAssertFalse(profile.settings.sharpen)
        XCTAssertEqual(profile.environment["BB_RENDERER"], "metal")
        XCTAssertEqual(profile.environment["BB_NATIVE_GPU"], "0")
        XCTAssertEqual(profile.environment["BB_LANGUAGE"], "11")
        XCTAssertEqual(profile.environment["BB_QUICKSAVE"], "1")
        prefs.renderer = "vulkan"
        let fallback = OptimizedProfile(prefs: prefs, game: game)
        XCTAssertEqual(fallback.settings.upscaler, "fsr3")
        XCTAssertTrue(fallback.settings.objectMotion)
        XCTAssertEqual(fallback.environment["BB_NATIVE_GPU"], "1")
        game.upscaler = "metalfx"
        XCTAssertEqual(OptimizedProfile(prefs: prefs, game: game).settings.upscaler, "fsr3")
    }

    func testMetalReadinessChecksSelectedBackendResources() throws {
        let root = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: root) }
        func executable(_ path: String) throws {
            let file = root.appendingPathComponent(path)
            try FileManager.default.createDirectory(at: file.deletingLastPathComponent(), withIntermediateDirectories: true)
            try Data().write(to: file)
            try FileManager.default.setAttributes([.posixPermissions: 0o755], ofItemAtPath: file.path)
        }
        for path in ["tools/macos/run.sh", "tools/macos/run_optimized.sh", "python/bin/python3"] {
            try executable(path)
        }
        XCTAssertFalse(checkOptimizedEngine(root, packaged: true, renderer: "metal").allowsPlay)
        try executable("bin/bb-probe-metal")
        try executable("bin/gpu-metal/libbbgpu.dylib")
        XCTAssertTrue(checkOptimizedEngine(root, packaged: true, renderer: "metal").allowsPlay)
        XCTAssertFalse(checkOptimizedEngine(root, packaged: true, renderer: "vulkan").allowsPlay)
    }

    func testEveryWideOptionHasAnExactCommunityPatch() throws {
        let root = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
            .deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
        let xml = try String(contentsOf: root.appendingPathComponent("patches/Bloodborne.xml"))
        for option in Catalog.wideResolutions {
            var prefs = LauncherPrefs()
            prefs.ultrawide = true
            prefs.wideResolution = option.value
            let profile = OptimizedProfile(prefs: prefs, game: GameSettings())
            let patch = try XCTUnwrap(profile.environment["BB_PATCHES"])
            XCTAssertTrue(patch.hasSuffix("(\(profile.aspect))"))
            XCTAssertNil(profile.environment["BB_DISPLAY_ASPECT"])
            XCTAssertTrue(xml.contains("Name=\"\(patch)\""), patch)
            XCTAssertEqual(profile.settings.upscaler, "off")
        }
    }

    func testQuicksaveIsOptInAndCanBeDisabledAgain() {
        var prefs = LauncherPrefs()
        XCTAssertEqual(OptimizedProfile(prefs: prefs, game: GameSettings()).environment["BB_QUICKSAVE"], "0")
        prefs.quicksave = true
        XCTAssertEqual(OptimizedProfile(prefs: prefs, game: GameSettings()).environment["BB_QUICKSAVE"], "1")
        prefs.quicksave = false
        XCTAssertEqual(OptimizedProfile(prefs: prefs, game: GameSettings()).environment["BB_QUICKSAVE"], "0")
    }

    func testDisplayAndSaveChoicesPersistAndInvalidSizesRecover() throws {
        let suite = "bbport.optimized.tests.\(UUID().uuidString)"
        let defaults = try XCTUnwrap(UserDefaults(suiteName: suite))
        defer { defaults.removePersistentDomain(forName: suite) }
        let firstLaunch = LauncherPrefs.load(from: defaults)
        XCTAssertFalse(firstLaunch.ultrawide)
        XCTAssertEqual(firstLaunch.resolution, "1920x1080")
        XCTAssertEqual(OptimizedProfile(prefs: firstLaunch, game: GameSettings()).settings.outputRes, "1920x1080")
        var prefs = LauncherPrefs()
        prefs.ultrawide = true
        prefs.wideResolution = "5120x1440"
        prefs.uiModFolder = "/tmp/My downloaded UI (21x9)"
        prefs.resolution = "2560x1440"
        prefs.quicksave = true
        prefs.save(to: defaults)
        XCTAssertEqual(LauncherPrefs.load(from: defaults), prefs)
        defaults.set("9000x500", forKey: "wideResolution")
        XCTAssertEqual(LauncherPrefs.load(from: defaults).wideResolution, "3840x1080")
    }

    func testInheritedBenchmarkAndOtherPortSettingsCannotLeak() {
        let clean = OptimizedProfile.cleanEnvironment([
            "HOME": "/home/test", "PATH": "/usr/bin", "BB_GPU_PROCESS": "1",
            "BB_QUICKLOAD_PENDING": "1", "BB_PAD_FILE": "/tmp/controller",
            "BB_DMEM_MB": "5056", "BB_QUICKSAVE": "1", "VK_DRIVER_FILES": "old-driver",
            "DYLD_LIBRARY_PATH": "other-port", "PYTHON": "other-python",
        ])
        XCTAssertEqual(clean, ["HOME": "/home/test", "PATH": "/usr/bin"])
    }

    func testIniPreservesControllerMappingsAndUnknownSettings() {
        let ini = IniFile(lines: ["pad.r1=custom", "unknown=keep", "output_res=3840x1080"], values: [:])
        let text = ini.text(applying: OptimizedProfile(prefs: LauncherPrefs(), game: GameSettings()).settings.iniValues)
        XCTAssertTrue(text.contains("pad.r1=custom\n"))
        XCTAssertTrue(text.contains("unknown=keep\n"))
        XCTAssertTrue(text.contains("output_res=1920x1080\n"))
    }
}
