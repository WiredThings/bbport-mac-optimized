// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <vector>

#include "common/types.h"
#include "common/unique_function.h"
#include "video_core/render_state.h"

// The scheduler features the rasterizer core uses (docs/metal/rasterizer_core_design.md, the queue
// interface; plan P4.1 R6). What the caches use of the scheduler (ticks, deferred operations,
// waits, host copies, EndRendering) is CacheBackend's and stays there. Vulkan::Scheduler
// implements this; the Metal scheduler will (P4.2). Plain types only.

namespace VideoCore {

/// What a submission takes along: the backend's own submit info derives from it (the Vulkan one
/// adds its semaphores and fence).
struct SubmitInfo {
    /// Queue operations that go out right before the submission, in queue order (sparse binds).
    std::vector<std::function<void()>> before_submit;
};

/// Called on every submission (the backend's rasterizer flushes its barriers and hands the
/// buffer cache's arena binds to the submission).
using SubmitFunc = Common::UniqueFunction<void, SubmitInfo&>;

class GpuQueue {
public:
    virtual ~GpuQueue() = default;

    // Submission

    /// Sends the current execution context to the GPU and advances the tick.
    virtual void Flush() = 0;

    /// Sets a function to be called on every submission.
    virtual void SetSubmitCallback(SubmitFunc&& on_submit) = 0;

    /// Waits until the submission signalling `tick` is in the queue (not for the GPU to finish).
    virtual void WaitSubmitted(u64 tick) = 0;

    /// The newest tick the GPU is known to have finished, as of the last refresh
    /// (CacheBackend::RefreshTicks).
    [[nodiscard]] virtual u64 KnownGpuTick() const noexcept = 0;

    /// Diagnostics (the GPU watchdog): prints what the GPU is stuck on, if the backend knows.
    virtual void ReportStuck(const char* where) {}

    // Recording order

    /// Hands recorded commands to the recording threads (no-op for immediate recording). Without
    /// `force` a long enough segment is cut at the end of a draw or dispatch.
    virtual void KickRecording(bool force = false) = 0;

    /// Waits until every recorded command is in the command buffer (no-op for immediate recording).
    virtual void SyncRecording() = 0;

    // The open render pass

    /// Whether a render pass with exactly this state is open.
    [[nodiscard]] virtual bool IsRenderingWith(const RenderState& state) const = 0;

    /// Whether beginning a render pass with this state would start a new pass.
    [[nodiscard]] virtual bool WillBeginRendering(const RenderState& state) const = 0;

    // Held passes (BB_HOIST_DISPATCH): the commands of an open render pass are kept back until it
    // ends, so a dispatch that touches nothing the pass's draws touched can go before it. The
    // accessors are plain inline reads (the core asks per draw and per buffer binding).

    [[nodiscard]] bool HoldingPass() const noexcept {
        return holding;
    }
    [[nodiscard]] bool HoistEnabled() const noexcept {
        return hold_passes;
    }
    /// Changes when a new render pass is held (the rasterizer's per-pass access sets).
    [[nodiscard]] u64 HeldPassSerial() const noexcept {
        return held_serial;
    }
    /// Commands recorded between BeginHoist and EndHoist go before the held render pass.
    virtual void BeginHoist() noexcept {}
    virtual void EndHoist() noexcept {}
    /// Puts the held render pass's commands in the stream; the pass goes on unheld.
    virtual void ReleaseHeldPass() {}

    // Host copies and signals

    /// Waits for the guest memory copies issued so far (before a write the guest can observe).
    virtual void WaitHostCopies() = 0;

    /// Waits for the host copies only when one of them may still read [address, address + size).
    virtual void WaitHostCopiesFor(u64 address, u64 size) = 0;

    /// Runs `signal` once the guest memory copies issued so far are done, without waiting here.
    virtual void SignalAfterHostCopies(std::function<void()> signal) = 0;

    /// Waits until every signal handed to SignalAfterHostCopies ran.
    virtual void WaitDeferredSignals() = 0;

protected:
    GpuQueue() = default;

    /// Set by the backend's scheduler (it holds passes only with deferred recording).
    bool hold_passes = false; ///< a new render pass is held (HoistEnabled)
    bool holding = false;     ///< the open render pass is held (HoldingPass)
    u64 held_serial = 0;      ///< counts held passes (HeldPassSerial)
};

} // namespace VideoCore
