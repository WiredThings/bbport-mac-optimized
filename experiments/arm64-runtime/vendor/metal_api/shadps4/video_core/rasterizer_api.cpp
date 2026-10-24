// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdio>
#include <cstdlib>
#include <string_view>
#include "bbport_toggles.h"
#include "video_core/buffer_cache/buffer.h"
#include "video_core/rasterizer_api.h"

namespace VideoCore {

bool RasterizerApi::HonestLabels() {
    static const bool on = [] {
        const char* env = std::getenv("BB_HONEST_LABELS");
        const bool enabled = (env && env[0] == '1') || GuestInPlace();
        if (enabled) {
            std::printf("GPU: fences written when the GPU has finished the work before them "
                        "(BB_HONEST_LABELS=1)\n");
        }
        return enabled;
    }();
    return on;
}

bool RasterizerApi::OcclusionTranslated() {
    // BB_OCCLUSION_QUERIES=1 (opt-in): Vulkan occlusion queries. Else the counters stay the fixed
    // sequence written when decoded (everything visible, as in the 0.3 model). With an upscaler
    // the scene is drawn smaller and jittered: a light's sub-pixel test shape passed samples in
    // one frame and none in the next, and its glow flashed grey over the whole frame (a lamp in
    // the cathedral at 640x360). Counting has to become stable against that first.
    // The PM4 self-test (BB_PM4_SELFTEST=1) checks them, so it turns them on.
    static const bool enabled = [] {
        const char* env = std::getenv("BB_OCCLUSION_QUERIES");
        const char* selftest = std::getenv("BB_PM4_SELFTEST");
        const char* metal = std::getenv("BB_METAL_OCCLUSION");
        const char* renderer = std::getenv("BB_RENDERER");
        const bool metal_enabled = metal && metal[0] == '1' && renderer &&
                                   std::string_view(renderer) == "metal";
        return (env && env[0] == '1') || (selftest && selftest[0] == '1') || metal_enabled;
    }();
    // Experiment bit 4 of BB_TOGGLE_FILE: the fixed sequence while the game runs (A/B).
    return enabled && GuestInPlace() && !BbToggle::Experiment(4);
}

} // namespace VideoCore
