// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"

// The backend's GPU-written guest memory services the rasterizer core asks for (plan P4.1 R6): the
// game's occlusion queries and the command processor's clock. The backend's rasterizer creates
// them on first use (RasterizerCore::CreateOcclusion, CreateTimestamps); plain types only.

namespace VideoCore {

/// The game's occlusion queries (ZPASS_DONE events) written into guest memory by the GPU.
class OcclusionApi {
public:
    virtual ~OcclusionApi() = default;

    /// A ZPASS_DONE event writing the counters of `pairs` depth blocks at `address` (recording
    /// thread, in stream order).
    virtual void Event(VAddr address, u32 pairs) = 0;

    /// Before a label (an end-of-pipe write the CPU may poll): the events recorded before it are
    /// written first, as on the PS4.
    virtual void WriteBeforeLabel() = 0;

    /// Events so far.
    [[nodiscard]] virtual u64 Events() const noexcept = 0;

    /// BB_OCCLUSION_READ_TRACE=1 (diagnostics): the game's first access to a page with counters
    /// (any thread). False: not such a page.
    virtual bool OnAccess(VAddr address, u64 rip, bool write, bool gpu_thread) = 0;
};

/// The command processor's GPU clock written into guest memory by the GPU.
class TimestampApi {
public:
    virtual ~TimestampApi() = default;

    /// The GPU clock written as a 64-bit value at `address` by the GPU in stream order: once the
    /// work before it is done (`end_of_pipe`), else when the GPU gets to it. False: no timestamps
    /// here (the caller writes it with the CPU).
    virtual bool Write(VAddr address, bool end_of_pipe) = 0;
};

} // namespace VideoCore
