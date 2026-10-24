// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project

#include <chrono>
#include <dlfcn.h>
#include <map>
#include <unordered_set>
#include <xxhash.h>
#include "video_core/ui_composition.h"
#include "bbport_timeline.h"
#include "bbport_sections.h"
#include "game_profile.h"
#include "bbport_ce_stats.h"
#include "bbport_toggles.h"
#include "bbport_flush_reason.h"
#include "bbport_write_log.h"
#include "bbport_free_check.h"
#include "bbport_guest_memory.h"
#include "video_core/frame_capture.h"
#include "common/hash.h"
#include "common/debug.h"
#include "core/debug_state.h"
#include "core/emulator_settings.h"
#include "core/memory.h"
#include "shader_recompiler/runtime_info.h"
#include "video_core/amdgpu/liverpool.h"
#include "video_core/amdgpu/pm4_cmds.h"
#include "video_core/buffer_cache/buffer.h"
#include "video_core/buffer_cache/buffer_cache.h"
#include "video_core/renderer_vulkan/liverpool_to_vk.h"
#include "video_core/rasterizer_core.h"
#include "bbport_threads.h"
#include "video_core/texture_cache/image_view.h"
#include "video_core/texture_cache/texture_cache.h"

// The draw pipeline's vocabulary (vk_draw_pipe.h, vk_draw_prep.h) and the translation helpers.
using Vulkan::DrawPipe;
using Vulkan::ImageDescHash;
using Vulkan::PreparedDraw;
using Vulkan::PreparedStage;
using Vulkan::PreparedVertexInputs;
namespace LiverpoolToVK = Vulkan::LiverpoolToVK;

namespace VideoCore {

bool RasterizerCore::WriteLabelOnGpu(VAddr address, u64 value, u32 num_bytes) {
    static const bool enabled = [] {
        const char* env = std::getenv("BB_GPU_LABELS");
        return !env || env[0] != '0';
    }();
    if (!enabled || !BufferMarkersSupported() || !VideoCore::GuestInPlace() || !HonestLabels() ||
        BbFreeCheck::Enabled() || (num_bytes != 4 && num_bytes != 8) ||
        address % num_bytes != 0) {
        return false;
    }
    const auto target = buffer_cache.GuestChunkSource(address, num_bytes);
    if (!target) {
        return false;
    }
    if (occlusion) {
        occlusion->WriteBeforeLabel(); // the occlusion results before it (stream order)
    }
    RecordBufferMarker(target->first, target->second, value, num_bytes);
    // A VRAM copy of these bytes gets them too, at its next binding (as for CPU-written labels).
    buffer_cache.NoteLateCommandWrite(address, &value, num_bytes);
    BbStats::gpu_labels.fetch_add(1, std::memory_order_relaxed);
    // In stream order for WAIT_REG_MEM, and the submission policy (HasUnsubmittedSignals).
    SignalAfterGpu([] {}, address, value);
    return true;
}

void RasterizerCore::GpuSignalLoop(std::stop_token token) {
    Common::SetCurrentThreadName("bb:GpuSignals");
    while (!token.stop_requested()) {
        u64 tick = 0;
        {
            std::unique_lock lk{gpu_signals_mutex};
            if (!gpu_signals_cv.wait(lk, token, [this] { return !gpu_signals.empty(); })) {
                return;
            }
            tick = gpu_signals.front().tick;
        }
        // Bounded waits: the tick may not be submitted yet (the GPU command thread submits it
        // when it runs out of work), and a stop request must be seen.
        // bbport: a lost device ends the wait with a report (it spun here forever: the game froze
        // with its sound playing, Steam Deck); 10 s without progress prints what is stuck once.
        const auto wait_start = std::chrono::steady_clock::now();
        bool watched = false;
        while (!token.stop_requested()) {
            if (WaitGpuTick(tick, 5'000'000)) {
                break;
            }
            if (!watched && std::chrono::steady_clock::now() - wait_start > std::chrono::seconds(10)) {
                watched = true;
                GpuWatchdog(tick);
            }
        }
        backend.RefreshTicks();
        BbTimeline::Note(BbTimeline::GpuDone, queue.KnownGpuTick());
        while (true) {
            std::function<void()> signal;
            {
                std::scoped_lock lk{gpu_signals_mutex};
                if (gpu_signals.empty() || !backend.IsFreeKnown(gpu_signals.front().tick)) {
                    break;
                }
                signal = std::move(gpu_signals.front().signal);
                // Kept queued while it runs: PendingSignalValue still sees the fence.
            }
            signal();
            std::scoped_lock lk{gpu_signals_mutex};
            gpu_signals.pop_front();
        }
    }
}

void RasterizerCore::PublishSignals() {
    // Only the recording thread, or the GPU command thread while it is idle (drained), touches
    // them; other threads (a guest thread reading back) leave them to those.
    const bool on_b = DrawPipe::OnStageB();
    if (!(on_b || (OnStageA() && DrawPipeIdle()))) {
        return;
    }
    // Fences of the packets before this one (stage B), or of all (stage A, drained).
    const u64 upto = draw_pipe ? (on_b ? draw_pipe->Consumed() : draw_pipe->Head()) : 0;
    if (local_signals.empty()) {
        signals_published_upto.store(upto, std::memory_order_release);
        return;
    }
    bool was_empty;
    {
        std::scoped_lock lk{gpu_signals_mutex};
        if (!gpu_signal_thread.joinable()) {
            gpu_signal_thread =
                std::jthread([this](std::stop_token token) { GpuSignalLoop(token); });
        }
        was_empty = gpu_signals.empty();
        for (auto& pending : local_signals) {
            gpu_signals.push_back(std::move(pending));
        }
    }
    local_signals.clear();
    local_signal_count.store(0, std::memory_order_release);
    signals_published_upto.store(upto, std::memory_order_release);
    if (was_empty) {
        gpu_signals_cv.notify_one();
    }
}

void RasterizerCore::SignalAfterGpu(std::function<void()> signal, VAddr label, u64 value) {
    if (DrawPipe::OnStageB()) {
        // Kept here until the end of the guest submission or the next flush (in tick order).
        local_signals.push_back({backend.CurrentTick(), std::move(signal), label, value});
        local_signal_count.store(u32(local_signals.size()), std::memory_order_release);
        return;
    }
    if (OnStageA() && DrawPipeIdle()) {
        PublishSignals(); // the recording thread's come first (it is idle)
    }
    bool was_empty;
    {
        std::scoped_lock lk{gpu_signals_mutex};
        if (!gpu_signal_thread.joinable()) {
            gpu_signal_thread =
                std::jthread([this](std::stop_token token) { GpuSignalLoop(token); });
        }
        was_empty = gpu_signals.empty();
        gpu_signals.push_back({backend.CurrentTick(), std::move(signal), label, value});
    }
    // The signal thread waits on the condition only with nothing queued (else it waits for the
    // GPU and takes the rest when it gets back): no notify per fence (~60 a frame).
    if (was_empty) {
        gpu_signals_cv.notify_one();
    }
}

void RasterizerCore::RequestSignalFlush() {
    if (signal_flush_requested.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    static constexpr u8 none = 0;
    RunInOrder(
        [](VideoCore::RasterizerApi& api, const u8*) {
            auto& self = static_cast<RasterizerCore&>(api);
            self.signal_flush_requested.store(false, std::memory_order_release);
            self.PublishSignals();
            if (self.HasUnsubmittedSignals()) {
                BbFlushReason::Scope reason{BbFlushReason::Reason::SignalFlushTask};
                self.Flush();
            }
        },
        &none, 0, BbToggle::PipelinedTasks, false);
}

bool RasterizerCore::SignalsPublished(u64 position) const {
    return !draw_pipe || !HonestLabels() ||
           signals_published_upto.load(std::memory_order_acquire) >= position;
}

bool RasterizerCore::HasUnsubmittedSignals() {
    if (local_signal_count.load(std::memory_order_acquire) != 0) {
        return true; // for the current tick, held by the recording thread
    }
    std::scoped_lock lk{gpu_signals_mutex};
    return !gpu_signals.empty() && gpu_signals.back().tick >= backend.CurrentTick();
}

bool RasterizerCore::PipeSignals() const {
    static const bool enabled = [] {
        const char* env = std::getenv("BB_PIPE_SIGNALS");
        return !env || env[0] != '0';
    }();
    return enabled && HonestLabels() && UseDrawPipe() && OnStageA();
}

void RasterizerCore::SubmitForSignals() {
    if (!HonestLabels()) {
        return;
    }
    if (PipeSignals()) {
        // Decided (and submitted) on the recording thread after this submission's draws and
        // fences: here they may not even be recorded yet.
        static constexpr u8 none = 0;
        RunInOrder(
            [](VideoCore::RasterizerApi& api, const u8*) {
                static_cast<RasterizerCore&>(api).SubmitForSignalsNow();
            },
            &none, 0, BbToggle::PipelinedTasks, false);
        return;
    }
    SubmitForSignalsNow();
}

void RasterizerCore::SubmitForSignalsNow() {
    PublishSignals(); // the end of a guest submission
    static const auto interval = std::chrono::microseconds([] {
        const char* env = std::getenv("BB_HONEST_FLUSH_US");
        return env ? std::strtoll(env, nullptr, 10) : 2000ll;
    }());
    if (!HasUnsubmittedSignals()) {
        return;
    }
    // Batched (one submission per BB_HONEST_FLUSH_US) while the GPU still has work; at once when it
    // has finished everything submitted: holding the work back then only leaves it idle. Not at
    // once while draws are queued for this thread (BB_HONEST_IDLE_BACKLOG bytes, default 64 KiB):
    // it is then what the GPU waits for, and a submission costs it ~65 us (heavy scenes: 13 a
    // frame; 95 -> 100 FPS with half as many).
    static const bool idle_flush = [] {
        const char* env = std::getenv("BB_HONEST_IDLE_FLUSH");
        return !env || env[0] != '0';
    }();
    static const u64 idle_backlog = [] {
        const char* env = std::getenv("BB_HONEST_IDLE_BACKLOG");
        return env ? std::strtoull(env, nullptr, 10) : u64(64 * 1024);
    }();
    const bool gpu_idle =
        idle_flush &&
        (!draw_pipe || !DrawPipe::OnStageB() || draw_pipe->Backlog() < idle_backlog) &&
        backend.IsFree(backend.CurrentTick() - 1);
    const auto since_flush = std::chrono::nanoseconds(
        std::chrono::steady_clock::now().time_since_epoch().count() -
        last_flush_ns.load(std::memory_order_relaxed));
    if (!gpu_idle && since_flush < interval) {
        return;
    }
    if (gpu_idle) {
        BbStats::idle_flushes.fetch_add(1, std::memory_order_relaxed);
    }
    BbFlushReason::Scope reason{gpu_idle ? BbFlushReason::Reason::SignalsGpuIdle
                                         : BbFlushReason::Reason::SignalsInterval};
    Flush();
}

bool RasterizerCore::PendingSignalValue(VAddr address, u64& value) {
    std::scoped_lock lk{gpu_signals_mutex};
    for (auto it = gpu_signals.rbegin(); it != gpu_signals.rend(); ++it) {
        if (it->label == address) {
            value = it->value;
            return true;
        }
    }
    return false;
}

void RasterizerCore::GpuWatchdog(u64 tick) {
    backend.RefreshTicks();
    const u64 done = queue.KnownGpuTick(), recording = backend.CurrentTick();
    if (tick < recording) {
        std::printf("GPU watchdog: submission %llu not finished after 10 s (the GPU finished %llu, "
                    "recording %llu): the GPU is stuck\n",
                    (unsigned long long)tick, (unsigned long long)done, (unsigned long long)recording);
        buffer_cache.ReportArenaBinds();
        queue.ReportStuck("submitted work not finished in 10 s");
    } else {
        std::printf("GPU watchdog: waited 10 s for submission %llu, not submitted yet (the GPU finished "
                    "%llu, recording %llu, draw pipe backlog %llu bytes): the port's threads are "
                    "waiting for each other\n",
                    (unsigned long long)tick, (unsigned long long)done, (unsigned long long)recording,
                    (unsigned long long)(draw_pipe ? draw_pipe->Backlog() : 0));
    }
    std::fflush(stdout);
}

void RasterizerCore::Finish() {
    DrainDrawPipe();
    PublishSignals();
    backend.Finish();
}

void RasterizerCore::OnSubmit() {
    DrainDrawPipe();
    if (fault_process_pending) {
        fault_process_pending = false;
        buffer_cache.ProcessFaultBuffer();
    }
    texture_cache.ProcessDownloadImages();
    texture_cache.RunGarbageCollector();
    buffer_cache.ReportBdaPageTable();
    backend.TickFrame();
}

} // namespace VideoCore
