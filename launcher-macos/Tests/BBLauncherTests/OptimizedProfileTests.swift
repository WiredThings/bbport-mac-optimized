import XCTest
@testable import BBLauncher

final class OptimizedProfileTests: XCTestCase {

    func testOriginalModelDetailForNewProfilesWithoutOverwritingExistingChoices() {
        XCTAssertEqual(GameSettings().modelLOD, "0")
        XCTAssertEqual(GameSettings(ini: [:]).modelLOD, "0")
        for value in ["-2", "0", "1", "2"] {
            let game = GameSettings(ini: ["model_lod": value])
            let profile = OptimizedProfile(prefs: LauncherPrefs(), game: game)
            XCTAssertEqual(profile.settings.modelLOD, value)
            XCTAssertEqual(profile.settings.iniValues.first { $0.key == "model_lod" }?.value, value)
        }
    }

    func testBackendCapabilitiesMatchPinnedMetalEngine() {
        XCTAssertFalse(Catalog.presentModes(renderer: "metal").contains { $0.value == "FifoRelaxed" })
        XCTAssertTrue(Catalog.presentModes(renderer: "vulkan").contains { $0.value == "FifoRelaxed" })
        XCTAssertEqual(Catalog.presets.map(\.value), [0, 5, 1, 2, 3, 4])
        var prefs = LauncherPrefs(); var game = GameSettings()
        prefs.presentMode = "FifoRelaxed"; prefs.resolution = "1280x720"
        game.upscaler = "metalfx"; game.preset = 3
        var profile = OptimizedProfile(prefs: prefs, game: game)
        XCTAssertEqual(profile.presentMode, "Mailbox") // matches old runtime fallback
        XCTAssertEqual(profile.metalRenderResolution, "640 × 360")
        XCTAssertEqual(profile.gpuProfileVariable, "BB_MTL_GPU_TIMING")
        XCTAssertEqual(profile.settings.preset, 3)
        prefs.resolution = "1920x1080"; game.preset = 2
        profile = OptimizedProfile(prefs: prefs, game: game)
        XCTAssertEqual(profile.metalRenderResolution, "1130 × 636")
        game.preset = 0
        XCTAssertEqual(OptimizedProfile(prefs: prefs, game: game).metalRenderResolution, "1920 × 1080")
        game.preset = 5
        XCTAssertEqual(OptimizedProfile(prefs: prefs, game: game).settings.preset, 5)
        XCTAssertEqual(OptimizedProfile(prefs: prefs, game: game).metalRenderResolution, "1536 × 864")
        prefs.renderer = "vulkan"
        profile = OptimizedProfile(prefs: prefs, game: game)
        XCTAssertEqual(profile.presentMode, "FifoRelaxed")
        XCTAssertEqual(profile.settings.preset, 5)
        XCTAssertEqual(profile.gpuProfileVariable, "BB_GPU_PROFILE")
    }

    func testMetalPresentChoicesRepresentDistinctRuntimeBehaviors() {
        XCTAssertEqual(Catalog.presentModes(renderer: "metal").map(\.value), ["Fifo", "Mailbox"])
        for mode in ["Mailbox", "Immediate", "FifoRelaxed"] {
            var prefs = LauncherPrefs(); prefs.presentMode = mode
            XCTAssertEqual(OptimizedProfile(prefs: prefs, game: GameSettings()).presentMode, "Mailbox")
            XCTAssertEqual(prefs.presentMode, mode)
            prefs.renderer = "vulkan"
            XCTAssertEqual(OptimizedProfile(prefs: prefs, game: GameSettings()).presentMode, mode)
        }
    }

    func testGameVelocityPersistenceAndBackendIsolation() {
        let suite = "bbport.velocity.tests.\(UUID().uuidString)"
        let defaults = UserDefaults(suiteName: suite)!
        defer { defaults.removePersistentDomain(forName: suite) }
        var prefs = LauncherPrefs.load(from: defaults)
        var game = GameSettings()
        game.upscaler = "metalfx"
        game.effects[game.effects.firstIndex { $0.key == "effect_motion_blur" }!].isOn = false
        XCTAssertTrue(prefs.metalObjectMotion)
        let profile = OptimizedProfile(prefs: prefs, game: game)
        XCTAssertEqual(profile.environment["BB_METAL_OBJECT_MOTION"], "1")
        XCTAssertFalse(profile.settings.effects.first { $0.key == "effect_motion_blur" }!.isOn)
        XCTAssertFalse(profile.settings.objectMotion)
        prefs.metalObjectMotion = false
        prefs.save(to: defaults)
        XCTAssertFalse(LauncherPrefs.load(from: defaults).metalObjectMotion)
        XCTAssertEqual(OptimizedProfile(prefs: prefs, game: game).environment["BB_METAL_OBJECT_MOTION"], "0")
        prefs.metalObjectMotion = true
        game.upscaler = "off"
        XCTAssertEqual(OptimizedProfile(prefs: prefs, game: game).environment["BB_METAL_OBJECT_MOTION"], "0")
        prefs.renderer = "vulkan"
        game.upscaler = "fsr3"
        XCTAssertEqual(OptimizedProfile(prefs: prefs, game: game).environment["BB_METAL_OBJECT_MOTION"], "0")
    }

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
