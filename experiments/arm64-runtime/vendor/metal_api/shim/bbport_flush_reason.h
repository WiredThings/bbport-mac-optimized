// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: why a command buffer was committed (BB_FRAME_STATS=1 prints the per-frame counts next to
// the other frame stats). The call sites that end up in a scheduler's Flush set a thread-local
// reason with a scope (the innermost wins; untagged is Other); the scheduler counts one commit
// under the current reason. Instrumentation only: nothing here changes what is submitted.
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <utility>

namespace BbFlushReason {

enum class Reason : unsigned {
    Other,            ///< untagged
    FrameEnd,         ///< Liverpool::SignalDecodedFrames: the frame's retirement signal
    SubmitDone,       ///< Liverpool::Process after submit_done (OnSubmit + Flush)
    DecoderIdle,      ///< the decoder thread idle for BB_HONEST_IDLE_US with signals pending
    SignalFlushTask,  ///< RequestSignalFlush's task on the recording thread
    SignalsGpuIdle,   ///< SubmitForSignalsNow: the GPU had finished everything submitted
    SignalsInterval,  ///< SubmitForSignalsNow: BB_HONEST_FLUSH_US elapsed since the last flush
    SpinMemSemaphore, ///< gfx MemSemaphore wait spinning with unsubmitted signals
    SpinWaitRegMem,   ///< gfx WaitRegMem spinning with unsubmitted signals
    SpinAsc,          ///< ASC MemSemaphore / WaitRegMem spinning with unsubmitted signals
    SyncForCpuRead,   ///< Liverpool::SyncForCpuRead (Finish)
    EventWriteEos,    ///< EVENT_WRITE_EOS GDS store (Finish)
    LayerCopyBack,    ///< BufferCache layer read trap: Wait / Finish for the copy back
    DownloadMemory,   ///< BufferCache::DownloadMemory: Wait / Finish
    DownloadImage,    ///< TextureCache sync DownloadImage (Finish)
    StreamBufferWait, ///< StreamBuffer::WaitPendingOperations
    PresenterPrepare, ///< Presenter::PrepareFrame's flush
    PresenterWait,    ///< the presenter's waits on a frame's tick
    SchedulerWait,    ///< Scheduler::Wait on the open tick, no caller tagged
    SchedulerFinish,  ///< Scheduler::Finish, no caller tagged
    Count,
};

inline constexpr std::array<const char*, static_cast<std::size_t>(Reason::Count)> Names = {
    "other",
    "frame end",
    "submit done",
    "decoder idle",
    "signal request",
    "signals GPU idle",
    "signals interval",
    "spin mem semaphore",
    "spin wait reg mem",
    "spin ASC",
    "sync for CPU read",
    "EOS GDS store",
    "layer copy back",
    "download memory",
    "download image",
    "stream buffer wait",
    "presenter prepare",
    "presenter wait",
    "scheduler wait",
    "scheduler finish",
};

inline thread_local Reason current = Reason::Other;

/// `BbFlushReason::Scope s{Reason::X};` tags the flushes made on this thread while it lives.
/// `keep_outer`: only when no caller tagged (for the scheduler's own Wait / Finish).
class Scope {
public:
    explicit Scope(Reason reason, bool keep_outer = false) : previous{current} {
        if (!keep_outer || previous == Reason::Other) {
            current = reason;
        }
    }
    ~Scope() {
        current = previous;
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    Reason previous;
};

struct Counters {
    std::array<std::atomic<std::uint64_t>, static_cast<std::size_t>(Reason::Count)> commits{};
    std::atomic<std::uint64_t> signal_only{}; ///< commits whose command buffer held only the signal
    std::atomic<std::uint64_t> skipped{};     ///< flushes with nothing to commit
};
inline Counters& Get() {
    static Counters counters;
    return counters;
}

/// A command buffer is committed for the current reason.
inline void Commit() {
    Get().commits[static_cast<std::size_t>(current)].fetch_add(1, std::memory_order_relaxed);
}
inline void SignalOnly() {
    Get().signal_only.fetch_add(1, std::memory_order_relaxed);
}
inline void Skipped() {
    Get().skipped.fetch_add(1, std::memory_order_relaxed);
}

/// Prints (and resets) the counts of a stats window of `frames` frames.
inline void Report(std::uint64_t frames) {
    auto& counters = Get();
    std::array<std::pair<std::uint64_t, std::size_t>, static_cast<std::size_t>(Reason::Count)> sorted;
    std::uint64_t total = 0;
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        sorted[i] = {counters.commits[i].exchange(0), i};
        total += sorted[i].first;
    }
    const std::uint64_t signal_only = counters.signal_only.exchange(0);
    const std::uint64_t skipped = counters.skipped.exchange(0);
    if (total == 0 && skipped == 0) {
        return;
    }
    const double per_frame = frames ? 1.0 / double(frames) : 0.0;
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
        return a.first != b.first ? a.first > b.first : a.second < b.second;
    });
    std::printf("Command buffers: %.1f/frame (", total * per_frame);
    bool first = true;
    for (const auto& [count, index] : sorted) {
        if (count != 0) {
            std::printf("%s%s %.1f", first ? "" : ", ", Names[index], count * per_frame);
            first = false;
        }
    }
    std::printf("%ssignal-only %.1f; skipped %.1f)\n", first ? "" : "; ", signal_only * per_frame,
                skipped * per_frame);
}

} // namespace BbFlushReason
