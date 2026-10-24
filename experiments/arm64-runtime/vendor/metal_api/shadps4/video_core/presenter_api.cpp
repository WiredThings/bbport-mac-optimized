// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "video_core/presenter_api.h"
#include "video_core/renderer_vulkan/vk_presenter.h"
#ifdef __APPLE__
#include "video_core/renderer_metal/mtl_presenter.h"
#endif

namespace VideoCore {

Backend RendererBackend() {
    static const Backend backend = [] {
        const char* env = std::getenv("BB_RENDERER");
        const Backend chosen =
            env && std::strcmp(env, "metal") == 0 ? Backend::Metal : Backend::Vulkan;
        std::printf("GPU: renderer backend %s\n", chosen == Backend::Metal ? "metal" : "vulkan");
        return chosen;
    }();
    return backend;
}

std::unique_ptr<PresenterApi> CreatePresenter(Frontend::WindowSDL& window,
                                              AmdGpu::Liverpool* liverpool) {
    if (RendererBackend() == Backend::Metal) {
#ifdef __APPLE__
        // renderer_metal/ uses macOS 15 APIs (residency sets); the process itself targets 14.
        if (__builtin_available(macOS 15.0, *)) {
            return std::make_unique<Metal::Presenter>(window, liverpool);
        }
        std::printf("GPU: BB_RENDERER=metal needs macOS 15, using vulkan\n");
#else
        std::printf("GPU: BB_RENDERER=metal is not available on this platform, using vulkan\n");
#endif
    }
    return std::make_unique<Vulkan::Presenter>(window, liverpool);
}

} // namespace VideoCore
