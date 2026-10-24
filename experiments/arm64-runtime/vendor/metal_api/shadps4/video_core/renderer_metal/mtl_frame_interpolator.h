// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <memory>

#include "common/types.h"

namespace Metal {

class Device;
class Scheduler;

/// What the MetalFX temporal scaler consumed for a frame, which the frame interpolator needs as
/// well (Upscaler::TakeInterpolationInputs): the scene depth and the motion vectors at input
/// resolution, the jitter and motion scale as handed to the scaler, the camera.
struct InterpolationInputs {
    const void* depth{};  ///< id<MTLTexture> (bridged): the game's scene depth
    const void* motion{}; ///< id<MTLTexture>: RG16Float, previous minus current, render pixels
    u32 input_width{}, input_height{};     ///< the size of both textures
    u32 content_width{}, content_height{}; ///< the part of them the scene fills (top-left)
    std::array<float, 2> jitter{};         ///< the scaler's jitterOffsetX/Y
    std::array<float, 2> motion_scale{};   ///< the scaler's motionVectorScaleX/Y
    float vertical_fov{};                  ///< degrees
    float near_plane{}, far_plane{};
    bool reset{}; ///< the scaler restarts its history (camera cut, resume after a menu, ...)
};

/// bbport: MetalFX frame interpolation (plan P7.2, BB_FRAME_INTERPOLATION=1, macOS 26+; off by
/// default). For the game frame N (the upscaled, UI composited display image) it encodes the
/// frame between N-1 and N into an output texture of its own; the presenter shows that and then
/// frame N. Standalone mode, not the combined one with the temporal scaler (the `scaler`
/// property of the descriptor): the UI is drawn on top of the scaler's output, so the image the
/// interpolator needs is not the scaler's. No separate UI texture exists (the game draws its UI
/// into the upscaled image): uiTexture stays nil and isUITextureComposited NO, so the HUD is
/// warped with the scene in the interpolated frame.
///
/// Frame N-1's image is kept as a copy owned here (the display buffers are reused by the game);
/// the interpolator, the copy and the output are released with Scheduler::DeferOperation (command
/// buffers do not retain). The interpolator is created on a worker thread (first creation takes
/// up to a second): frames meanwhile are not interpolated.
///
/// Threads: everything but Requested() is called by the thread that records on the draw
/// scheduler (PrepareFrame), one at a time.
class FrameInterpolator {
public:
    /// BB_FRAME_INTERPOLATION=1 on a device that supports it (macOS 26+, supportsDevice): when
    /// it was asked for and cannot run, one line says why and the answer is false. Presenter
    /// constructor; `device` is the MTLDevice (bridged).
    static bool Requested(const void* device);

    FrameInterpolator(Device& device, Scheduler& scheduler);
    ~FrameInterpolator();

    FrameInterpolator(const FrameInterpolator&) = delete;
    FrameInterpolator& operator=(const FrameInterpolator&) = delete;

    /// The frame N-1 -> N: `color` is the id<MTLTexture> (bridged) of frame N, `delta_time` the
    /// game's frame interval in seconds, `reset` that history cannot be relied on (a pause, the
    /// first frame after a gap). Encodes the interpolator and then the copy of `color` that frame
    /// N+1 reads, into the scheduler's open command buffer (the open encoder is ended). Returns
    /// the output texture (id<MTLTexture>, valid until the next Run, same format and size as
    /// `color`, ShaderRead) or null when no frame is generated (first frame, interpolator not
    /// ready, settings it cannot take); the copy is made either way.
    const void* Run(const void* color, const InterpolationInputs& inputs, double delta_time,
                    bool reset);
    /// The frames are not coming through the scaler any more (a menu, a loading screen, no
    /// scaling): the next Run starts the history again.
    void Invalidate();

private:
    struct Impl;
    Scheduler& scheduler;
    std::unique_ptr<Impl> impl;
};

} // namespace Metal
