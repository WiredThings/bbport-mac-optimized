// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <atomic>
#include <optional>
#include <vector>
#include <boost/container/small_vector.hpp>

#include "common/types.h"
#include "video_core/cache_backend.h"
#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/renderer_vulkan/vk_semaphore.h"

VK_DEFINE_HANDLE(VmaAllocator)
VK_DEFINE_HANDLE(VmaAllocation)

namespace Vulkan {

class Instance;
class Scheduler;

/// The Vulkan side of the buffer cache's sparse arena (VideoCore::SparseArena): residency memory
/// blocks (vkAllocateMemory), sparse binds (vkQueueBindSparse on the graphics queue, with the
/// timeline semaphore the submissions wait for). Moved out of VideoCore::BufferCache.
class SparseArena final : public VideoCore::SparseArena {
public:
    SparseArena(const Instance& instance, Scheduler& scheduler);
    ~SparseArena() override;

    u64 BlockAlignment() const noexcept override {
        return block_alignment;
    }
    bool HasFallbackMemory() const noexcept override {
        return arena_fallback_type_index.has_value();
    }
    VideoCore::ArenaAllocation Allocate(u64 size) override;
    VideoCore::ArenaMemory AllocateFallback(u64 size) override;
    VideoCore::ArenaMemory AllocateOtherType(u64 size) override;
    void Free(VideoCore::ArenaMemory memory) override;
    void QueueBinds(const VideoCore::Buffer* arena,
                    std::span<const VideoCore::SparseBind> binds) override;
    bool HasPendingBinds() const noexcept override {
        return !pending_binds.empty();
    }
    void SubmitBinds(SubmitInfo& info, u64 wait_tick) override;
    VideoCore::ArenaBindStats BindStats() const noexcept override {
        return {arena_bind_calls.load(), arena_bind_ranges.load()};
    }
    void ReportBinds() override;

private:
    struct ArenaBinds {
        const VideoCore::Buffer* arena;
        boost::container::small_vector<vk::SparseMemoryBind, 32> binds;
    };

    ArenaBinds* BindsForArena(const VideoCore::Buffer* arena) {
        auto it = std::ranges::find(pending_binds, arena, &ArenaBinds::arena);
        if (it != pending_binds.end()) {
            return std::addressof(*it);
        }
        return &pending_binds.emplace_back(arena);
    }

    const Instance& instance;
    Scheduler& scheduler;
    std::vector<ArenaBinds> pending_binds;
    Semaphore memory_semaphore;
    u64 block_alignment{};
    u32 arena_memory_type_index{};
    /// bbport: system memory for residency when VRAM is full (4 GB cards), if sparse binding takes it
    std::optional<u32> arena_fallback_type_index;
    u32 arena_memory_type_bits = 0; ///< memory types the arena's blocks can be bound to
    std::atomic<u64> arena_bind_calls{0}, arena_bind_ranges{0}; ///< vkQueueBindSparse calls, ranges
    std::atomic<u64> arena_bind_signaled{0}; ///< memory_semaphore value the last call signals
};

/// The Vulkan implementation of VideoCore::FastReadback: copies on the readback queue (Instance).
class FastReadback final : public VideoCore::FastReadback {
public:
    FastReadback(const Instance& instance, Scheduler& scheduler);
    ~FastReadback() override;

    bool Available() const noexcept override {
        return bool(queue);
    }
    const u8* Copy(const VideoCore::Buffer* arena, std::span<const vk::BufferCopy> copies,
                   u64 total, u64 tick) override;

private:
    bool Ensure(u64 size);

    Scheduler& scheduler;
    vk::Device device;
    vk::Queue queue;
    VmaAllocator allocator;
    vk::UniqueCommandPool pool;
    vk::CommandBuffer cmdbuf;
    vk::UniqueFence fence;
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    u8* mapped = nullptr;
    u64 capacity = 0;
};

} // namespace Vulkan
