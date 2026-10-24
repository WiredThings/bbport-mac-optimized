// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

#include "common/types.h"
#include "video_core/scene_passes.h"

// bbport (plan P5.3, docs/metal/ui_composition_design.md): the UI composition of a scaled session
// (BB_RENDER_RES): the game's UI is drawn at the output size into images of the port instead of
// at the render size into the game's targets. The decisions and state live here and are shared by
// the backends; the images and their GPU work stay behind UiCompositionBackend (Vulkan:
// TemporalUpscaler, Metal: Metal::Upscaler). Plain types only: the views cross as NativeView.

namespace VideoCore {

/// What the UI composition asks of a backend. Called on the threads that call the compositor.
class UiCompositionBackend {
public:
    virtual ~UiCompositionBackend() = default;

    /// (Re)creates the output-size UI color image (`new_color`: game color format `color`;
    /// sampled, render target, transfer src/dst) and/or the UI depth/stencil image (`new_depth`:
    /// format `depth`), `width` x `height`. May wait for the GPU.
    virtual void EnsureUiImages(u32 width, u32 height, vk::Format color, vk::Format depth,
                                bool new_color, bool new_depth) = 0;
    /// The game's frame (the top-left `width` x `height` of `color`; the live path of the
    /// Vulkan backend reads a scene proxy of it instead) scaled with linear filtering into the
    /// whole UI color image, ready as a color attachment after.
    virtual void CopyBackground(ImageId color, u32 width, u32 height) = 0;
    /// Clears the UI depth (1) and stencil (0); with `copy` the depth aspect of the game's
    /// `depth` is scaled (nearest) into it first. Ready as a depth attachment after.
    virtual void PrepareUiDepth(ImageId depth, bool copy) = 0;
    /// Whether PrepareUiDepth can copy a game depth image of this format.
    [[nodiscard]] virtual bool CanCopyDepth(vk::Format depth_format) const = 0;
    /// Views of the UI color image in the game's view format and swizzle, and its layout.
    virtual ReplacementView UiColorView(vk::Format format, vk::ComponentMapping mapping) = 0;
    virtual ReplacementView UiDepthView() = 0;
    /// After the UI phase, before the display pass samples the UI color image.
    virtual void UiReadBarrier() = 0;
    /// The output-size host display image standing in for the guest display buffer at `address`
    /// (created or recreated when its format or size changed), as a color attachment. Called
    /// with the compositor's display lock held, as is the `use` callback of DisplayOverride.
    virtual ReplacementView DisplayTarget(VAddr address, vk::Format image_format,
                                          vk::Format view_format, u32 width, u32 height) = 0;
};

/// What the compositor looks up of the backend's image cache.
struct UiImageInfo {
    vk::Format format{};
    u32 width{}, height{};
    VAddr address{};
};

struct UiCompositorEnv {
    std::function<UiImageInfo(ImageId)> image_info;
    /// Whether the guest address is a display buffer (FrameCapture::IsDisplayBuffer).
    std::function<bool(VAddr)> is_display_buffer;
};

class UiCompositor {
public:
    /// Reads BB_OUTPUT_RES, BB_RENDER_RES, BB_UI_TRIGGER_VS and BB_UI_TRACE.
    UiCompositor(UiCompositionBackend& backend, UiCompositorEnv env);

    /// Render size below the scaled-preset output size (the resolution patch is on).
    [[nodiscard]] bool Scaled() const {
        return scaled_session || output_width != 1920 || output_height != 1080;
    }
    /// BB_RENDER_RES is set.
    [[nodiscard]] bool ScaledSession() const noexcept {
        return scaled_session;
    }
    /// A target of the patched render size: the game allocates it with aligned dimensions
    /// (a 1916x1078 scene in 1916x1080 targets).
    [[nodiscard]] bool RenderTarget(u32 w, u32 h) const {
        if (!scaled_session) return w == 1920 && h == 1080;
        return w >= render_width && h >= render_height && w < render_width + 8 &&
               h < render_height + 8;
    }
    [[nodiscard]] u32 OutputWidth() const noexcept { return output_width; }
    [[nodiscard]] u32 OutputHeight() const noexcept { return output_height; }
    [[nodiscard]] u32 RenderWidth() const noexcept { return render_width; }
    [[nodiscard]] u32 RenderHeight() const noexcept { return render_height; }
    [[nodiscard]] u64 TriggerVs() const noexcept { return ui_trigger_vs; }
    /// From the upscale (or the background copy) to the next display pass.
    [[nodiscard]] bool UiPhase() const noexcept { return ui_phase; }
    /// The live output size changed (a menu setting, not in a scaled session).
    void SetOutputSize(u32 width, u32 height) {
        output_width = width;
        output_height = height;
    }
    /// The scene targets' size on the live path.
    void SetRenderSize(u32 width, u32 height) {
        render_width = width;
        render_height = height;
    }

    /// Start of a frame (display pass): the display pass of a frame with UI is redirected.
    void OnFrameStart();

    /// After identifying a draw's targets, before drawing any UI pixels. A UI draw starts the UI
    /// phase: with the temporal background (`temporal_ready`: the scene is complete and
    /// `color` is the frame; `temporal_active`: the upscaler is on) `run_temporal` upscales the
    /// frame and calls Begin; when it did not, or without it, the game's frame is copied.
    /// Returns true when it started the UI phase with the copy (the caller restarts its history).
    bool OnDraw(u64 vs_hash, ImageId color, ImageId depth, bool native_viewport,
                bool temporal_ready, bool temporal_active, FunctionRef<void()> run_temporal);

    /// (Re)creates the UI images when the size or a format changed (backend EnsureUiImages) and
    /// prints the composition line then.
    void EnsureImages(u32 width, u32 height, vk::Format color, vk::Format depth);
    /// Clears the UI depth, with a copy of the game's `depth` when the backend can.
    void PrepareDepth(ImageId depth);
    /// The background is in the UI image and the UI depth is prepared: the UI phase begins.
    void Begin(ImageId color, ImageId depth) {
        ui_phase = true;
        ui_color = color;
        ui_depth = depth;
    }

    /// Render target redirection (UI passes, display pass): false when not redirected.
    /// `view_info` is the game's view of the image: redirected views mirror its format (sRGB)
    /// and, for sampling, its channel swizzle.
    bool RedirectColor(ImageId color, const ImageViewInfo& view_info, UpscalerTarget& target);
    bool RedirectDepth(ImageId depth, UpscalerTarget& target);
    /// A sampled image replaced by the UI image (the display pass).
    bool RedirectSampled(ImageId image, const ImageViewInfo& info, NativeView& view,
                         vk::ImageLayout& layout);
    /// Whether RedirectSampled would replace `image` (it may record a barrier then).
    [[nodiscard]] bool RedirectsSampled(ImageId image) const {
        return display_redirect && image == ui_color;
    }
    /// The bits of the state RedirectColor/RedirectDepth depend on (ui_phase, display_redirect,
    /// the UI images), for memoizing a draw's render state; bit 2 (done_this_frame) is the caller's.
    [[nodiscard]] u64 RedirectState() const noexcept {
        return u64(ui_phase) | u64(display_redirect) << 1 | u64(ui_color.index) << 8 |
               u64(ui_depth.index) << 36;
    }

    /// Presenter: whether the output-size display buffer stands in for the guest one at
    /// `address`; then `use` is called with its format and size under the display lock (the
    /// backend reads its image there).
    bool DisplayOverride(VAddr address, FunctionRef<void(vk::Format, u32, u32)> use);

private:
    UiCompositionBackend& backend;
    UiCompositorEnv env;

    u32 output_width = 1920, output_height = 1080; ///< BB_OUTPUT_RES, the UI image size
    u32 render_width = 1920, render_height = 1080; ///< BB_RENDER_RES
    bool scaled_session = false;
    u64 ui_trigger_vs = 0x34e8a281;
    ImageId ui_color{}, ui_depth{};
    bool ui_phase = false;
    bool display_redirect = false; ///< the display pass of a frame with UI
    bool ui_read_barrier = false;
    u32 ui_width = 0, ui_height = 0;
    vk::Format ui_format = vk::Format::eUndefined;
    vk::Format ui_depth_format = vk::Format::eUndefined;
    const bool ui_trace_on; ///< BB_UI_TRACE diagnostics
    std::string ui_trace;

    struct Display {
        vk::Format format{};
        u32 width = 0, height = 0;
        bool valid = false;
    };
    std::mutex display_mutex;
    std::unordered_map<VAddr, Display> displays;
};

/// BB_PRESENT_DUMP_TRIGGER=<file>: whether the frame being presented is to be saved (consumes
/// the trigger file). BB_PRESENT_DUMP_COUNT=N: the trigger dumps N consecutive frames. Called on
/// the presenting thread.
bool PresentDumpDue();

} // namespace VideoCore
