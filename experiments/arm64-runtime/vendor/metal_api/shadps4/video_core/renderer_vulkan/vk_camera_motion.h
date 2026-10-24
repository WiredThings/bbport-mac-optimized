// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: camera motion vectors for temporal upscaling (docs/upscaler.md). The scene constants
// give the camera of each frame; the previous one is kept here. BB_DEBUG_MOTION=1 blends the
// motion vectors as colors into the frame before it is copied to the display.

#pragma once

#include <array>
#include <vector>

#include "common/types.h"
#include "video_core/camera_tracker.h"
#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/buffer_cache/buffer.h"
#include "video_core/texture_cache/image.h"

namespace VideoCore {
class TextureCache;
}

namespace Vulkan {

class Instance;
class Scheduler;
class Runtime;
class ObjectMotion;

class CameraMotion {
public:
    CameraMotion(const Instance& instance, Scheduler& scheduler,
                 VideoCore::TextureCache& texture_cache, Runtime& runtime);
    ~CameraMotion();
    void InvalidateHistory() { tracker.InvalidateHistory(); }
    void SetObjectMotion(ObjectMotion* motion) noexcept {
        object_motion = motion;
    }
    /// This frame's object motion image (after RecordMotion), for the debug view; or null.
    vk::ImageView ObjectMotionView() const noexcept;
    vk::Image ObjectMotionImage(u32 width, u32 height) const noexcept;

    void SetJitter(std::array<float, 2> value) noexcept {
        tracker.SetJitter(value);
    }

    [[nodiscard]] bool Enabled() const noexcept {
        return debug_overlay || for_upscaler;
    }

    /// Both cameras and the scene depth of the current frame are known.
    [[nodiscard]] bool Ready() const noexcept {
        return tracker.Ready();
    }
    [[nodiscard]] VideoCore::ImageId Depth() const noexcept {
        return tracker.Depth();
    }
    /// The render size in the scene constants (the game's viewport; its targets may be
    /// allocated larger, e.g. 1916x1080 for a 1916x1078 scene), or zero before the first camera.
    [[nodiscard]] std::array<u32, 2> RenderSize() const noexcept {
        return tracker.RenderSize();
    }
    /// Vertical field of view and near/far planes of the current camera.
    [[nodiscard]] float VerticalFov() const noexcept {
        return tracker.VerticalFov();
    }
    [[nodiscard]] float Near() const noexcept {
        return tracker.Near();
    }
    // Current projection, previous projection, and row Z of previous-view * inverse-view.
    [[nodiscard]] std::array<std::array<float, 4>, 3> TaaDepthParameters() const noexcept {
        return tracker.TaaDepthParameters();
    }
    /// bbport: the matrices and jitter of the motion of this frame, printed with an upscaler dump
    /// (BB_DUMP_TRIGGER) for offline checks of the vectors.
    void PrintState(int frame) const {
        tracker.PrintState(frame);
    }

    /// Records the motion vector pass (Scheduler::Record): `depth_view` (depth aspect, General
    /// layout) into `motion_view` (RG16F storage, General), pixels, previous minus current.
    void RecordMotion(vk::ImageView depth_view, vk::ImageView motion_view, u32 width, u32 height);

    /// A bound constant buffer of 864 bytes: checks the scene constant signature.
    void OnConstants(const float* data);

    /// The G-buffer pass (5+ color targets): its depth is the scene depth.
    /// `x_sign`/`y_sign`: signs of the G-buffer pass's viewport x/y scale (window = ndc * scale +
    /// offset). The motion shaders take ndc as +y down the screen, so the projection's scales are
    /// multiplied by them: the game's viewport decides which way view +y goes, per frame.
    void OnGBufferPass(VideoCore::ImageId depth, float x_sign = 1.0f, float y_sign = 1.0f);

    /// The pass copying the finished frame (`frame`, the last target drawn) to the display: the
    /// frame's depth and camera are complete. Records the debug overlay, starts a new frame.
    void OnDisplayPass(VideoCore::ImageId frame);

private:
    /// `state`: the tracker as it was when the display pass began (OnDisplayPass resets it).
    void Overlay(VideoCore::ImageId frame, const VideoCore::CameraTracker& state);

    const Instance& instance;
    Scheduler& scheduler;
    VideoCore::TextureCache& texture_cache;
    Runtime& runtime;
    bool debug_overlay = false;
    bool for_upscaler = false;
    vk::UniqueDescriptorSetLayout motion_desc_layout;
    vk::UniquePipelineLayout motion_pipeline_layout;
    vk::UniquePipeline motion_pipeline;

    ObjectMotion* object_motion = nullptr;
    VideoCore::CameraTracker tracker;

    vk::UniqueDescriptorSetLayout desc_layout;
    vk::UniquePipelineLayout pipeline_layout;
    vk::UniquePipeline overlay_pipeline;
    /// Debug: the last two finished frames (RGBA8 packed), for the reprojection check.
    std::unique_ptr<VideoCore::Buffer> frames[2];
    u32 frame_index = 0;
};

} // namespace Vulkan
