// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <memory>

#include "video_core/renderer_metal/mtl_frame_interpolator.h"
#include "video_core/scene_passes.h"
#include "video_core/ui_compositor.h"

namespace VideoCore {
class TextureCache;
}

namespace Metal {

class CameraMotion;
class Device;
class Runtime;
class SceneTargets;
class Scheduler;
class VelocityMirror;

/// bbport: the scene services of the Metal rasterizer (plan P5.3, P5.4 and P7.5, docs/metal/
/// ui_composition_design.md, temporal_upscaling_design.md and live_resolution_design.md).
/// Counterpart: Vulkan::TemporalUpscaler (vk_temporal_upscaler.cpp) with its UpscalerAdapter, cut
/// down to MetalFX as the only temporal provider (Vulkan: FSR 3, TemporalUpscaler::Run and
/// RunScaled):
///  - a scaled session (BB_RENDER_RES): the UI composition, and the MetalFX temporal scaler as
///    its `Temporal` background (RunScaled) on the game's LDR frame;
///  - the live path (no scaled session, P7.5): the guest renders at 1920x1080, the scene passes
///    are drawn into the reduced proxies of the SceneTargets at the preset's size, and the
///    post-processing combine shader triggers RunLive, which upscales the HDR scene (MetalFX,
///    from the proxies, or at 1:1 from the native images at Native AA) and merges the result
///    into the game's scene colour (upscale_merge.comp: RGB replaced, alpha kept). There is no
///    reactive mask (MetalFX has one since macOS 14.4: a follow-up), no frame interpolation
///    inputs and no object motion.
/// Without the MetalFX choice there is no temporal provider: the UI phase starts with the copy
/// of the game's frame and nothing is jittered.
///
/// The UI composition's decisions are the shared VideoCore::UiCompositor's; this class owns the
/// textures behind it (UiCompositionBackend, Metal textures in private storage), a few small
/// render passes (the background copy, the depth copy), the frame bookkeeping of the scaler
/// (jitter, history reset, the frame's LDR target) and the MTLFXTemporalScaler with the motion
/// vector texture. The scaler is created on a worker thread at startup (the first creation takes
/// seconds); until it is ready the frame is copied. The live path's scalers (one per input size,
/// output size and formats, a few kept) are created on a thread when the preset changes; until
/// the scaler of the new size is ready the frame is not upscaled (the scene proxy is resolved
/// into the native image by the nearest copy: a blurry frame or two, no stall).
///
/// Threads: everything but DisplayOverride is called by the thread that records draws (the draw
/// recording thread, else the GPU command thread: DrawRecord, which calls OnFrameStart, OnDraw
/// and the render target setup with RedirectColor/RedirectDepth/RedirectSampled in this order, is
/// the thread that owns the draw scheduler's immediate encoding). DisplayOverride is called by the
/// presenter, which drains the draw pipe first and records on the same scheduler.
///
/// Lifetime: command buffers do not retain what they use, so a texture (or a view of it) or the
/// scaler that is replaced or destroyed is released with Scheduler::DeferOperation.
class Upscaler final : public VideoCore::UpscalerApi, public VideoCore::UiCompositionBackend {
public:
    Upscaler(Device& device, Scheduler& scheduler, Runtime& runtime,
             VideoCore::TextureCache& texture_cache, CameraMotion& camera,
             SceneTargets& scene_targets);
    /// Joins the scaler's creation thread, then releases the scaler and the textures once the GPU
    /// is done with them (deferred).
    ~Upscaler() override;

    // VideoCore::UpscalerApi
    bool Enabled() const noexcept override {
        return enabled;
    }
    bool OnFrameStart() override;
    bool RasterScaling() const override;
    u64 RedirectState() const noexcept override {
        return ui.RedirectState() | u64(done_this_frame) << 2;
    }
    std::array<float, 2> Jitter() const noexcept override {
        return done_this_frame ? std::array<float, 2>{} : jitter;
    }
    float SceneMipBias() const override;
    void OnColorTarget(VideoCore::ImageId color) override;
    void OnSceneColor(VideoCore::ImageId color) override {
        scene_color = color;
    }
    void OnBlendedSceneDraw() override {}
    void OnSceneComposite() override {}
    void OnDispatch(u64 cs_hash) override;
    void OnDraw(u64 vs_hash, VideoCore::ImageId color, VideoCore::ImageId depth,
                bool native_viewport) override;
    bool RedirectColor(VideoCore::ImageId color, const VideoCore::ImageViewInfo& view,
                       VideoCore::UpscalerTarget& target) override {
        return ui.RedirectColor(color, view, target);
    }
    bool RedirectDepth(VideoCore::ImageId depth, VideoCore::UpscalerTarget& target) override {
        return ui.RedirectDepth(depth, target);
    }
    bool RedirectSampled(VideoCore::ImageId image, const VideoCore::ImageViewInfo& info,
                         VideoCore::NativeView& view, vk::ImageLayout& layout) override {
        return ui.RedirectSampled(image, info, view, layout);
    }
    bool RedirectsSampled(VideoCore::ImageId image) const override {
        return ui.RedirectsSampled(image);
    }

    // VideoCore::UiCompositionBackend
    void EnsureUiImages(u32 width, u32 height, vk::Format color, vk::Format depth, bool new_color,
                        bool new_depth) override;
    void CopyBackground(VideoCore::ImageId color, u32 width, u32 height) override;
    void PrepareUiDepth(VideoCore::ImageId depth, bool copy) override;
    bool CanCopyDepth(vk::Format depth_format) const override;
    VideoCore::ReplacementView UiColorView(vk::Format format,
                                           vk::ComponentMapping mapping) override;
    VideoCore::ReplacementView UiDepthView() override;
    void UiReadBarrier() override;
    VideoCore::ReplacementView DisplayTarget(VAddr address, vk::Format image_format,
                                             vk::Format view_format, u32 width,
                                             u32 height) override;

    /// The output-size host display texture standing in for a guest display buffer.
    struct DisplayImage {
        const void* texture{}; ///< id<MTLTexture> (bridged), in `format`; owned by the upscaler
        vk::Format format{};
        u32 width{}, height{};
    };
    /// Presenter: whether the host display texture stands in for the guest buffer at `address`.
    /// The texture stays valid until the next draw is recorded (the presenter uses it at once).
    bool DisplayOverride(VAddr address, DisplayImage& display);

    /// Presenter (P7.2, frame interpolation): what the MetalFX scaler consumed for the frame
    /// that was recorded last, if it ran for it; taken once (false until the scaler runs again).
    /// The textures are valid until the next draw is recorded.
    bool TakeInterpolationInputs(InterpolationInputs& inputs);

    void SetVelocityMirror(VelocityMirror* mirror) { velocity_mirror = mirror; }
    bool WantsVelocity() const { return Active() && !done_this_frame; }
    std::array<u32, 2> VelocitySize() const;

private:
    VelocityMirror* velocity_mirror = nullptr;
    struct Impl; ///< the textures, views, pipelines and the scaler (mtl_upscaler.mm)

    /// MetalFX is the selected provider (in a scaled session once its scaler is taken) and has
    /// not failed.
    [[nodiscard]] bool Active() const;
    /// The size the game rendered the scene at (the top-left of its targets of `w` x `h`).
    [[nodiscard]] std::array<u32, 2> SceneSize(u32 w, u32 h) const;
    /// The live path at the post-processing combine shader: upscales the scene colour (HDR) and
    /// merges the result into it; the frame stays as it is when the scaler is not ready.
    void RunLive();
    /// Starts the creation of the live scaler of the next frame's configuration when the formats
    /// of the last one are known (the preset or the output size changed).
    void PrepareLive();
    /// The temporal background of the UI composition: upscales the frame into the UI color
    /// texture and begins the UI phase; without Begin the compositor copies the frame instead.
    void RunScaled();

    Scheduler& scheduler;
    Runtime& runtime;
    VideoCore::TextureCache& texture_cache;
    CameraMotion& camera;
    SceneTargets& scene_targets;
    bool enabled = true;
    bool metalfx_supported = false;     ///< the device has the temporal scaler
    bool done_this_frame = false;
    bool failed = false;                ///< the scaler cannot run: the frame is copied
    bool reset = true;                  ///< the scaler restarts its history
    bool dispatched_last_frame = false;
    bool last_active = false, last_jitter = false;
    int applied_upscaler = -1;
    int applied_preset = -1;
    int applied_output = -1;
    unsigned preset_file_frames = 0; ///< BB_PRESET_FILE polling
    u64 trigger_hash = 0x9a9cf8a9;   ///< the post-processing combine shader (BB_UPSCALE_BEFORE_CS)
    VideoCore::ImageId scene_color{}; ///< this frame's HDR scene colour (OnSceneColor)
    u32 jitter_index = 0;
    std::array<float, 2> jitter{}; ///< this frame's sub-pixel jitter in render pixels
    /// The frame's last RGBA8 target of the depth's size before the UI (OnColorTarget).
    VideoCore::ImageId ldr_target{};
    InterpolationInputs interpolation; ///< RunScaled's, for the frame interpolator
    bool interpolation_valid = false;
    /// The format of the UI depth texture (as the game's), for CanCopyDepth.
    vk::Format ui_depth_format = vk::Format::eUndefined;
    VideoCore::UiCompositor ui;
    std::unique_ptr<Impl> impl;
};

} // namespace Metal
