// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the camera of each frame, from the scene constants (docs/upscaler.md), and the push
// constants of the camera motion pass (host_shaders/camera_motion.comp). Backend neutral: the
// Vulkan and Metal camera motion classes own one and record the pass themselves.

#pragma once

#include <array>
#include <cstddef>

#include "common/types.h"
#include "video_core/texture_cache/types.h"

namespace VideoCore {

/// The push constant block of camera_motion.comp (std430 push layout, 108 bytes).
struct MotionConstants {
    std::array<float, 12> reproject;  ///< previous view * inverse current view, 3x4 rows
    std::array<float, 4> proj;        ///< current: x scale, y scale, z scale, z offset
    std::array<float, 4> prev_proj;   ///< previous: the same
    std::array<float, 2> size;        ///< pixels
    std::array<float, 2> jitter;      ///< current viewport shift in render pixels (x right, y down)
    std::array<float, 2> previous_jitter;
    u32 mode;                         ///< 1: object motion available (motion pass); debug modes
};
static_assert(sizeof(MotionConstants) == 108);
static_assert(offsetof(MotionConstants, reproject) == 0);
static_assert(offsetof(MotionConstants, proj) == 48);
static_assert(offsetof(MotionConstants, prev_proj) == 64);
static_assert(offsetof(MotionConstants, size) == 80);
static_assert(offsetof(MotionConstants, jitter) == 88);
static_assert(offsetof(MotionConstants, previous_jitter) == 96);
static_assert(offsetof(MotionConstants, mode) == 104);

/// a * b for 3x4 affine matrices (rows [R | t]).
std::array<float, 12> Multiply(const std::array<float, 12>& a, const std::array<float, 12>& b);
/// The inverse of a 3x4 affine matrix [M | t]: [M^-1 | -M^-1 t]; `m` itself when singular.
std::array<float, 12> InverseAffine(const std::array<float, 12>& m);

class CameraTracker {
public:
    struct Camera {
        std::array<float, 12> view{};     ///< world to view, 3x4 rows
        std::array<float, 12> inv_view{}; ///< view to world, 3x4 rows
        std::array<float, 4> proj{};      ///< x scale, y scale, z scale, z offset
        bool valid = false;
    };

    void InvalidateHistory() {
        current.valid = previous.valid = false;
    }

    void SetJitter(std::array<float, 2> value) noexcept {
        previous_jitter = jitter;
        jitter = value;
    }

    /// Both cameras and the scene depth of the current frame are known.
    [[nodiscard]] bool Ready() const noexcept {
        return current.valid && previous.valid && depth_id;
    }
    [[nodiscard]] ImageId Depth() const noexcept {
        return depth_id;
    }
    /// The render size in the scene constants (the game's viewport; its targets may be
    /// allocated larger, e.g. 1916x1080 for a 1916x1078 scene), or zero before the first camera.
    [[nodiscard]] std::array<u32, 2> RenderSize() const noexcept {
        return render_size;
    }
    /// Vertical field of view and near plane of the current camera.
    [[nodiscard]] float VerticalFov() const noexcept;
    [[nodiscard]] float Near() const noexcept;
    // Current projection, previous projection, and row Z of previous-view * inverse-view.
    [[nodiscard]] std::array<std::array<float, 4>, 3> TaaDepthParameters() const noexcept;
    /// bbport: the matrices and jitter of the motion of this frame, printed with an upscaler dump
    /// (BB_DUMP_TRIGGER) for offline checks of the vectors.
    void PrintState(int frame) const;

    [[nodiscard]] const Camera& Current() const noexcept {
        return current;
    }
    [[nodiscard]] const Camera& Previous() const noexcept {
        return previous;
    }

    /// The push constants of the motion pass (or debug pass `mode`) for a `width` x `height`
    /// target, from the committed cameras and the jitter.
    [[nodiscard]] MotionConstants Constants(u32 width, u32 height, u32 mode) const;

    /// A bound constant buffer of 864 bytes: checks the scene constant signature.
    void OnConstants(const float* data);

    /// The G-buffer pass (5+ color targets): its depth is the scene depth.
    /// `x_sign`/`y_sign`: signs of the G-buffer pass's viewport x/y scale (window = ndc * scale +
    /// offset). The motion shaders take ndc as +y down the screen, so the projection's scales are
    /// multiplied by them: the game's viewport decides which way view +y goes, per frame.
    void OnGBufferPass(ImageId depth, float x_sign = 1.0f, float y_sign = 1.0f);

    /// Makes the camera of the G-buffer pass in progress the frame's camera, once per frame (the
    /// motion pass or the display pass).
    void CommitFrameCamera();

    /// The pass copying the finished frame to the display: the frame's depth and camera are
    /// complete. Commits the camera, starts a new frame. True when both cameras and the depth
    /// were valid before that (the debug overlay can be drawn).
    bool OnDisplayPass();

private:
    Camera current, previous;
    bool frame_has_camera = false;
    /// bbport: the camera of the G-buffer pass in progress (OnConstants), committed as the frame's
    /// camera once per frame (CommitFrameCamera).
    Camera pass_camera;
    bool pass_has_camera = false;
    bool committed = false;
    u32 frame_gbuffer_passes = 0;
    std::array<std::array<float, 3>, 4> pass_positions{};
    std::array<float, 2> jitter{}, previous_jitter{};
    std::array<u32, 2> render_size{};
    ImageId depth_id{};
    float gbuffer_x_sign = 1.0f; ///< bbport: signs of the G-buffer viewport scales (OnGBufferPass)
    float gbuffer_y_sign = 1.0f;
};

} // namespace VideoCore
