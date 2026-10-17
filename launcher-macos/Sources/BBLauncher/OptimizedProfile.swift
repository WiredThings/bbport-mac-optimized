// SPDX-License-Identifier: GPL-2.0-or-later
import Foundation

/// Display choices are independent of the container resolution in bbport.ini. The community
/// wide patches set scene/UI viewports and camera coordinates; output_res must stay 1080p.
/// UI assets come from the selected game installation, independently of display choices.
struct OptimizedProfile {
    let prefs: LauncherPrefs
    let game: GameSettings

    var resolution: String { prefs.ultrawide ? prefs.wideResolution : prefs.resolution }

    var aspect: String {
        if !prefs.ultrawide { return "16:9" }
        return ["2560x1080", "3440x1440"].contains(resolution) ? "21:9" : "32:9"
    }

    var settings: GameSettings {
        var settings = game
        settings.outputRes = prefs.ultrawide ? "1920x1080" : prefs.resolution
        settings.liveResolution = "0"
        if prefs.renderer == "metal" {
            if !["off", "metalfx"].contains(settings.upscaler) { settings.upscaler = "metalfx" }
            settings.objectMotion = false
            settings.sharpen = false
        } else if settings.upscaler == "metalfx" {
            settings.upscaler = "fsr3"
        }
        if prefs.ultrawide {
            settings.upscaler = "off"
            settings.preset = 0
        }
        return settings
    }

    var environment: [String: String] {
        var env = [
            "BB_LANGUAGE": prefs.language,
            "BB_NATIVE_GPU": prefs.renderer == "metal" ? "0" : "1", "BB_PREBUILT": "1",
            "BB_RENDERER": prefs.renderer,
            "BB_SKIP_EMPTY_DRAWS": "1", "BB_VK_RECORD_THREADS": "1",
            "BB_FRAMES_AHEAD": "2", "BB_GPU_QOS": "1",
            "BB_MAC_UNIFIED_MEMORY": "0", "BB_PIPE_PARK_WAITS": "0",
            "BB_QUICKSAVE": prefs.quicksave ? "1" : "0",
            "BB_QUICKSAVE_SCANCODE": String(prefs.quicksaveKey.code),
            "BB_QUICKLOAD_SCANCODE": String(prefs.quickloadKey.code),
            "BB_MENU_SCANCODE": String(prefs.menuKey.code),
            "BB_UPSCALER": settings.upscaler, "BB_UPSCALE_PRESET": String(settings.preset),
            "BB_LIVE_RES": "0", "BB_PATCHES": "",
        ]
        if prefs.ultrawide {
            env["BB_PATCHES"] = "Resolution Patch \(resolution) (\(aspect))"
            env["BB_DMEM_MB"] = "9152"
        }
        return env
    }

    /// Ignore inherited debug input and stale paths from a different port. Explicit developer
    /// overrides are parsed later, then the app's display/save/runtime settings win.
    static func cleanEnvironment(_ inherited: [String: String]) -> [String: String] {
        inherited.filter { key, _ in
            !key.hasPrefix("BB_") && !["VK_DRIVER_FILES", "VK_ICD_FILENAMES",
                "DYLD_LIBRARY_PATH", "SDL_VULKAN_LIBRARY", "MESA_SHADER_CACHE_DIR", "PYTHON"].contains(key)
        }
    }
}
