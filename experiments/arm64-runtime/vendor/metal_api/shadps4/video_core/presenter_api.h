// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>

#include "common/types.h"

namespace AmdGpu {
struct Liverpool;
}

namespace Frontend {
class WindowSDL;
}

namespace Libraries::VideoOut {
struct BufferAttributeGroup;
}

namespace VideoCore {

/// bbport: a frame of a backend's presenter; videoout only passes it back to the presenter.
struct PresentFrame {};

/// bbport: what videoout and GnmDriver call on a renderer backend's presenter. The presenter
/// owns the backend (its rasterizer binds itself to Liverpool and the memory manager). The
/// methods keep the Vulkan backend's contracts (vk_presenter.h).
class PresenterApi {
public:
    virtual ~PresenterApi() = default;

    virtual bool IsHDRSupported() const = 0;
    virtual void SetHDR(bool enable) = 0;
    /// The game's gamma setting (sceVideoOutAdjustColor).
    virtual void SetGamma(float gamma) = 0;

    virtual void RegisterVideoOutSurface(const Libraries::VideoOut::BufferAttributeGroup& attribute,
                                         VAddr cpu_address) = 0;
    virtual PresentFrame* PrepareFrame(const Libraries::VideoOut::BufferAttributeGroup& attribute,
                                       VAddr cpu_address) = 0;
    virtual PresentFrame* PrepareBlankFrame(bool present_thread) = 0;
    virtual PresentFrame* PrepareLastFrame() = 0;
    virtual void Present(PresentFrame* frame, bool is_reusing_frame = false,
                         bool is_game_frame = true) = 0;
    /// Waits until the GPU has finished the commands that render `frame` (any thread).
    virtual void WaitRendered(const PresentFrame* frame) = 0;
};

/// The presenter of the backend chosen by BB_RENDERER (RendererBackend()).
std::unique_ptr<PresenterApi> CreatePresenter(Frontend::WindowSDL& window,
                                              AmdGpu::Liverpool* liverpool);

enum class Backend { Vulkan, Metal };
/// BB_RENDERER=vulkan|metal, read once (default: vulkan).
Backend RendererBackend();

} // namespace VideoCore
