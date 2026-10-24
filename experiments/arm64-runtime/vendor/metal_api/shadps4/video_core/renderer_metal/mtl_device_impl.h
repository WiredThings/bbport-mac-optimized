// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#ifndef __OBJC__
#error "mtl_device_impl.h is for Objective-C++ (.mm) files only"
#endif

#import <Metal/Metal.h>

#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <vector>
#include "video_core/renderer_metal/mtl_device.h"

namespace Metal {

struct Device::Impl {
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    id<MTLResidencySet> residency; ///< attached to the queue; every GPU-visible buffer is added
    std::mutex residency_mutex;    ///< residency sets are not thread-safe
    /// The guest memory chunks' buffers (GuestMemory adds them), declared used by every encoder
    /// (Scheduler); `guest_count` is published after the entry is written.
    static constexpr std::size_t MaxGuestBuffers = 64;
    std::array<__unsafe_unretained id<MTLResource>, MaxGuestBuffers> guest_buffers{};
    std::atomic<std::size_t> guest_count{0};
    void AddGuestBuffer(id<MTLBuffer> buffer) {
        std::scoped_lock lock{residency_mutex};
        const std::size_t n = guest_count.load(std::memory_order_relaxed);
        if (n < MaxGuestBuffers) {
            guest_buffers[n] = buffer; // the chunk keeps it alive for the process
            guest_count.store(n + 1, std::memory_order_release);
        }
    }
    // Indirect page-table leaves need explicit per-encoder read declarations for tracked
    // hazards, not residency alone. The cache owns them; this list retains no resource.
    std::mutex bda_mutex;
    std::vector<__unsafe_unretained id<MTLResource>> bda_leaves;
    void BdaLeaf(id<MTLBuffer> buffer,bool attach) {
        std::scoped_lock lock{bda_mutex};
        if(attach)bda_leaves.push_back(buffer);
        else std::erase(bda_leaves,buffer);
    }
    bool request_each_commit = false; ///< BB_MTL_REQUEST_EACH=1 (P6.1 experiment)
    /// BB_FRAME_STATS: residency set commits, printed every 5 s (under residency_mutex).
    const bool commit_stats = std::getenv("BB_FRAME_STATS") != nullptr;
    unsigned long long commits = 0;
    std::chrono::steady_clock::time_point commits_since = std::chrono::steady_clock::now();
    void NoteCommit() {
        if (!commit_stats) {
            return;
        }
        ++commits;
        const auto now = std::chrono::steady_clock::now();
        if (now - commits_since >= std::chrono::seconds(5)) {
            std::printf("Metal residency set: %llu commits in the last %.1f s, %llu allocations\n",
                        commits, std::chrono::duration<double>(now - commits_since).count(),
                        (unsigned long long)residency.allocationCount);
            commits = 0;
            commits_since = now;
        }
    }

    /// Makes `allocation` resident for every command buffer committed from now on (D6: device
    /// addresses into it read and write nothing without that).
    void AddResident(id<MTLAllocation> allocation) {
        std::scoped_lock lock{residency_mutex};
        NoteCommit();
        [residency addAllocation:allocation];
        [residency commit];
        if (request_each_commit) {
            [residency requestResidency];
        }
    }
    /// Only once no command buffer that may use it is pending (the caller's deferred free).
    void RemoveResident(id<MTLAllocation> allocation) {
        std::scoped_lock lock{residency_mutex};
        NoteCommit();
        [residency removeAllocation:allocation];
        [residency commit];
    }
};

} // namespace Metal
