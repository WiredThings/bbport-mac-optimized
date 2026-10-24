// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <cstdlib>
#include <deque>

#include "bbport_toggles.h"
#include "common/types.h"

namespace VideoCore {

/// bbport: the GPU command thread runs at most BB_FRAMES_AHEAD (default 1) guest frames ahead of
/// the GPU: after a frame's commands are committed it waits for the frame that many flips back.
/// When the GPU is the bottleneck it finishes frames at an even rate; without this bound the
/// command thread ran ahead and then blocked wherever a resource ran out, so flips (and the
/// guest's frame timing) came in bursts: 12.5/25 ms alternation at 80 FPS. 0 turns it off.
/// One thread at a time (the one that prepares the frames). Backend neutral: the wait is a
/// callable taking the tick.
class FramesAhead {
public:
    /// Notes the draw tick of the frame just committed and, when the bound is reached, calls
    /// `wait(tick)` for the tick of the frame `frames_ahead` flips back (timed into
    /// BbStats::present_wait_ns).
    template <typename WaitFn>
    void Throttle(u64 frame_tick, WaitFn&& wait) {
        const u32 frames_ahead = Limit();
        if (!frames_ahead) {
            return;
        }
        recent_frame_ticks.push_back(frame_tick);
        while (recent_frame_ticks.size() > frames_ahead) {
            const u64 tick = recent_frame_ticks.front();
            recent_frame_ticks.pop_front();
            if (recent_frame_ticks.size() == frames_ahead) {
                BbStats::WaitTimer timer{BbStats::present_wait_ns};
                wait(tick);
            }
        }
    }

private:
    /// BB_FRAMES_AHEAD, read once.
    static u32 Limit() {
        static const u32 frames_ahead = [] {
            const char* env = std::getenv("BB_FRAMES_AHEAD");
            return env ? u32(std::max(0, std::atoi(env))) : 1u;
        }();
        return frames_ahead;
    }

    std::deque<u64> recent_frame_ticks;
};

} // namespace VideoCore
