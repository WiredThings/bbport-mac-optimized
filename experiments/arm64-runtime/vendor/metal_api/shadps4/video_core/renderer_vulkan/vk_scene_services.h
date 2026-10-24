// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>

#include "video_core/renderer_vulkan/vk_camera_motion.h"
#include "video_core/renderer_vulkan/vk_object_motion.h"
#include "video_core/renderer_vulkan/vk_scene_resolution.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_temporal_upscaler.h"
#include "video_core/scene_passes.h"

// bbport (plan P4.1 R6): the Vulkan scene services behind the interfaces the rasterizer core uses
// (video_core/scene_passes.h). The classes themselves are unchanged; each adapter forwards to its
// class and turns the Vulkan image views of the results into the core's opaque NativeView values.

namespace Vulkan {

class SceneTargetsAdapter final : public VideoCore::ScenePasses {
public:
    explicit SceneTargetsAdapter(SceneTargets& targets_) : targets{targets_} {}

    bool Tracks(u64 image_uid) const override {
        return targets.Tracks(image_uid);
    }
    bool Eligible(const VideoCore::Image& image) const override {
        return targets.Eligible(image);
    }
    bool EligibleScene(const VideoCore::Image& image) const override {
        return targets.EligibleScene(image);
    }
    SceneResolution::Size ProxySize(const VideoCore::Image& image, u32 level) const override {
        return targets.ProxySize(image, level);
    }
    SceneResolution::Size Size() const override {
        return targets.Size();
    }
    bool Reduced() const override {
        return targets.Reduced();
    }
    u64 Generation() const noexcept override {
        return targets.Generation();
    }
    VideoCore::ReplacementView Attachment(VideoCore::ImageId image,
                                          const VideoCore::ImageViewInfo& view_info) override {
        const auto target = targets.Attachment(image, view_info);
        return {NativeViewOf(target.view), target.layout};
    }
    std::optional<VideoCore::ReplacementView> SampleProxy(
        const VideoCore::Image& image, const VideoCore::ImageViewInfo& view_info) override {
        if (const auto proxy = targets.SampleProxy(image, view_info)) {
            return VideoCore::ReplacementView{NativeViewOf(proxy->view), proxy->layout};
        }
        return std::nullopt;
    }
    bool ProxyCurrent(const VideoCore::Image& image, u32 level, u32 levels) const override {
        return targets.ProxyCurrent(image, level, levels);
    }
    vk::ImageLayout PrepareSample(const VideoCore::Image& image, u32 level) override {
        return targets.PrepareSample(image, level);
    }
    void SetDebug(bool debug) override {
        targets.debug = debug;
    }

private:
    SceneTargets& targets;
};

class UpscalerAdapter final : public VideoCore::UpscalerApi {
public:
    explicit UpscalerAdapter(TemporalUpscaler& upscaler_) : upscaler{upscaler_} {}

    bool Enabled() const noexcept override {
        return upscaler.Enabled();
    }
    bool OnFrameStart() override {
        return upscaler.OnFrameStart();
    }
    bool RasterScaling() const override {
        return upscaler.RasterScaling();
    }
    u64 RedirectState() const noexcept override {
        return upscaler.RedirectState();
    }
    std::array<float, 2> Jitter() const noexcept override {
        return upscaler.Jitter();
    }
    float SceneMipBias() const override {
        return upscaler.SceneMipBias();
    }
    void OnColorTarget(VideoCore::ImageId color) override {
        upscaler.OnColorTarget(color);
    }
    void OnSceneColor(VideoCore::ImageId color) override {
        upscaler.OnSceneColor(color);
    }
    void OnBlendedSceneDraw() override {
        upscaler.OnBlendedSceneDraw();
    }
    void OnSceneComposite() override {
        upscaler.OnSceneComposite();
    }
    void OnDispatch(u64 cs_hash) override {
        upscaler.OnDispatch(cs_hash);
    }
    void OnDraw(u64 vs_hash, VideoCore::ImageId color, VideoCore::ImageId depth,
                bool native_viewport) override {
        upscaler.OnDraw(vs_hash, color, depth, native_viewport);
    }
    bool RedirectColor(VideoCore::ImageId color, const VideoCore::ImageViewInfo& view,
                       VideoCore::UpscalerTarget& target) override {
        TemporalUpscaler::Target redirect{};
        const bool redirected = upscaler.RedirectColor(color, view, redirect);
        target = Convert(redirect);
        return redirected;
    }
    bool RedirectDepth(VideoCore::ImageId depth, VideoCore::UpscalerTarget& target) override {
        TemporalUpscaler::Target redirect{};
        const bool redirected = upscaler.RedirectDepth(depth, redirect);
        target = Convert(redirect);
        return redirected;
    }
    bool RedirectSampled(VideoCore::ImageId image, const VideoCore::ImageViewInfo& info,
                         VideoCore::NativeView& view, vk::ImageLayout& layout) override {
        vk::ImageView redirected = ImageViewOf(view);
        const bool replaced = upscaler.RedirectSampled(image, info, redirected, layout);
        view = NativeViewOf(redirected);
        return replaced;
    }
    bool RedirectsSampled(VideoCore::ImageId image) const override {
        return upscaler.RedirectsSampled(image);
    }

private:
    static VideoCore::UpscalerTarget Convert(const TemporalUpscaler::Target& target) {
        return {NativeViewOf(target.view), target.layout, target.width, target.height,
                target.native_ui};
    }

    TemporalUpscaler& upscaler;
};

class CameraMotionAdapter final : public VideoCore::CameraMotionApi {
public:
    explicit CameraMotionAdapter(CameraMotion& camera_) : camera{camera_} {}

    bool Enabled() const noexcept override {
        return camera.Enabled();
    }
    VideoCore::ImageId Depth() const noexcept override {
        return camera.Depth();
    }
    void InvalidateHistory() override {
        camera.InvalidateHistory();
    }
    void SetJitter(std::array<float, 2> value) noexcept override {
        camera.SetJitter(value);
    }
    void OnConstants(const float* data) override {
        camera.OnConstants(data);
    }
    void OnGBufferPass(VideoCore::ImageId depth, float x_sign, float y_sign) override {
        camera.OnGBufferPass(depth, x_sign, y_sign);
    }
    void OnDisplayPass(VideoCore::ImageId frame) override {
        camera.OnDisplayPass(frame);
    }

private:
    CameraMotion& camera;
};

class ObjectMotionAdapter final : public VideoCore::ObjectMotionApi {
public:
    explicit ObjectMotionAdapter(ObjectMotion& motion_) : motion{motion_} {}

    void OnFrameStart() override {
        motion.OnFrameStart();
    }
    void InvalidateHistory() override {
        motion.InvalidateHistory();
    }
    bool Moving(const Motion::History::GateKey& key, u64 palette) override {
        return motion.Moving(key, palette);
    }
    Motion::IndexRangeCache::Result IndexRange(
        const Motion::IndexRangeCache::Key& key,
        VideoCore::FunctionRef<Motion::IndexRangeCache::Result()> scan) override {
        return motion.IndexRange(key, scan);
    }
    u32 PrepareDraw(const Motion::Draw& draw) override {
        return motion.PrepareDraw(draw);
    }
    void Attach(VideoCore::RenderState& state, u32 width, u32 height) override {
        motion.Attach(state, width, height);
    }

private:
    ObjectMotion& motion;
};

/// The Vulkan rasterizer's scene services: the objects and the adapters the core uses them by.
struct SceneServices {
    SceneServices(const Instance& instance, Scheduler& scheduler, Runtime& runtime,
                  VideoCore::TextureCache& texture_cache) {
        targets = std::make_unique<SceneTargets>(instance, scheduler, runtime, texture_cache);
        // Object motion first: it fixes the buffer addresses the motion shader variants embed.
        object_motion = std::make_unique<ObjectMotion>(instance, scheduler);
        camera_motion =
            std::make_unique<CameraMotion>(instance, scheduler, texture_cache, runtime);
        camera_motion->SetObjectMotion(object_motion.get());
        upscaler = std::make_unique<TemporalUpscaler>(instance, scheduler, texture_cache, runtime,
                                                      *camera_motion, *targets);
        targets_adapter = std::make_unique<SceneTargetsAdapter>(*targets);
        upscaler_adapter = std::make_unique<UpscalerAdapter>(*upscaler);
        camera_adapter = std::make_unique<CameraMotionAdapter>(*camera_motion);
        object_adapter = std::make_unique<ObjectMotionAdapter>(*object_motion);
    }

    // In the order the core used to destroy them.
    std::unique_ptr<CameraMotion> camera_motion;
    std::unique_ptr<SceneTargets> targets;
    std::unique_ptr<ObjectMotion> object_motion;
    std::unique_ptr<TemporalUpscaler> upscaler;
    std::unique_ptr<SceneTargetsAdapter> targets_adapter;
    std::unique_ptr<UpscalerAdapter> upscaler_adapter;
    std::unique_ptr<CameraMotionAdapter> camera_adapter;
    std::unique_ptr<ObjectMotionAdapter> object_adapter;
};

} // namespace Vulkan
