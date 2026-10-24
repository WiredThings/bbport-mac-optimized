// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <memory>

#include "bbport_guest_memory.h"
#include "common/alignment.h"
#include "common/assert.h"
#include "video_core/buffer_cache/buffer.h"
#include "video_core/renderer_vulkan/vk_breadcrumbs.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_platform.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_sparse_arena.h"

#include <vk_mem_alloc.h>

namespace Vulkan {

namespace {

/// The size of the buffer the sparse block size is probed with: one arena page of the buffer cache
/// (BufferCache::ARENA_PAGE_SIZE).
constexpr auto ARENA_PAGE_SIZE = u64{1} << 32;

constexpr auto ARENA_USAGE =
    vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst |
    vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eStorageBuffer |
    vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eVertexBuffer |
    vk::BufferUsageFlagBits::eIndirectBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress;

std::optional<u32> FindMemoryType(const vk::PhysicalDeviceMemoryProperties& properties,
                                  vk::MemoryPropertyFlags wanted, u32 memory_type_bits) {
    for (u32 i = 0; i < properties.memoryTypeCount; ++i) {
        if (((memory_type_bits >> i) & 1) == 0) {
            continue;
        }
        const auto flags = properties.memoryTypes[i].propertyFlags;
        if ((flags & wanted) == wanted) {
            return i;
        }
    }
    return std::nullopt;
}

VideoCore::ArenaMemory ToArena(vk::DeviceMemory memory) {
    return std::bit_cast<VideoCore::ArenaMemory>(memory);
}

vk::DeviceMemory FromArena(VideoCore::ArenaMemory memory) {
    return std::bit_cast<vk::DeviceMemory>(memory);
}

} // namespace

SparseArena::SparseArena(const Instance& instance_, Scheduler& scheduler_)
    : instance{instance_}, scheduler{scheduler_}, memory_semaphore{instance_} {
    const std::array<u32, 2> families = {instance.GetGraphicsQueueFamilyIndex(),
                                         instance.GetReadbackQueueFamilyIndex()};
    const bool shared = bool(instance.GetReadbackQueue()); // as Buffer creates the arenas
    const vk::BufferCreateInfo probe_ci = {
        .flags =
            vk::BufferCreateFlagBits::eSparseBinding | vk::BufferCreateFlagBits::eSparseResidency,
        .size = ARENA_PAGE_SIZE,
        .usage = ARENA_USAGE,
        .sharingMode = shared ? vk::SharingMode::eConcurrent : vk::SharingMode::eExclusive,
        .queueFamilyIndexCount = shared ? 2u : 0u,
        .pQueueFamilyIndices = shared ? families.data() : nullptr,
    };
    const vk::DeviceBufferMemoryRequirements req_info = {
        .pCreateInfo = &probe_ci,
    };
    const auto device = instance.GetDevice();
    const auto reqs = device.getBufferMemoryRequirements(req_info).memoryRequirements;
    block_alignment = reqs.alignment;
    arena_memory_type_index =
        FindMemoryType(instance.GetMemoryProperties(), vk::MemoryPropertyFlagBits::eDeviceLocal,
                       reqs.memoryTypeBits)
            .value();
    arena_memory_type_bits = reqs.memoryTypeBits;
    {
        const auto& properties = instance.GetMemoryProperties();
        for (u32 i = 0; i < properties.memoryTypeCount; ++i) {
            const auto flags = properties.memoryTypes[i].propertyFlags;
            if (((reqs.memoryTypeBits >> i) & 1) && !(flags & vk::MemoryPropertyFlagBits::eDeviceLocal) &&
                (flags & vk::MemoryPropertyFlagBits::eHostVisible)) {
                arena_fallback_type_index = i;
                break;
            }
        }
    }
}

SparseArena::~SparseArena() = default;

VideoCore::ArenaAllocation SparseArena::Allocate(u64 size) {
    auto vram = instance.GetDevice().allocateMemory({.allocationSize = size,
                                                     .memoryTypeIndex = arena_memory_type_index});
    VideoCore::ArenaAllocation allocation{};
    allocation.memory = ToArena(vram.value);
    if (vram.result == vk::Result::eSuccess) {
        allocation.status = VideoCore::ArenaAllocStatus::Success;
    } else {
        allocation.status = vram.result == vk::Result::eErrorOutOfDeviceMemory ||
                                    vram.result == vk::Result::eErrorOutOfHostMemory
                                ? VideoCore::ArenaAllocStatus::OutOfMemory
                                : VideoCore::ArenaAllocStatus::Failed;
        allocation.error = vk::to_string(vram.result);
    }
    return allocation;
}

VideoCore::ArenaMemory SparseArena::AllocateFallback(u64 size) {
    return ToArena(Vulkan::Check(instance.GetDevice().allocateMemory(
        {.allocationSize = size, .memoryTypeIndex = *arena_fallback_type_index})));
}

VideoCore::ArenaMemory SparseArena::AllocateOtherType(u64 size) {
    const auto& types = instance.GetMemoryProperties();
    for (u32 i = 0; i < types.memoryTypeCount; ++i) {
        if (((arena_memory_type_bits >> i) & 1) && i != arena_memory_type_index) {
            auto other = instance.GetDevice().allocateMemory({.allocationSize = size, .memoryTypeIndex = i});
            if (other.result == vk::Result::eSuccess) {
                return ToArena(other.value);
            }
        }
    }
    return 0;
}

void SparseArena::Free(VideoCore::ArenaMemory memory) {
    instance.GetDevice().freeMemory(FromArena(memory));
}

void SparseArena::QueueBinds(const VideoCore::Buffer* arena,
                             std::span<const VideoCore::SparseBind> binds) {
    ArenaBinds* arena_binds = BindsForArena(arena);
    for (const auto& bind : binds) {
        arena_binds->binds.emplace_back(vk::SparseMemoryBind{
            .resourceOffset = bind.resource_offset,
            .size = bind.size,
            .memory = FromArena(bind.memory),
            .memoryOffset = bind.memory_offset,
        });
    }
}

void SparseArena::SubmitBinds(SubmitInfo& info, u64 wait_tick) {
    const auto wait_sema = scheduler.GetWorkSemaphore()->Handle();
    const u64 signal_tick = memory_semaphore.NextTick();
    const auto signal_sema = memory_semaphore.Handle();
    info.AddWait(signal_sema, signal_tick);

    // The binds go into the queue right before the submission (BB_ASYNC_SUBMIT: later, from a
    // recording thread), after the submissions still on their way there.
    auto binds = std::make_shared<std::vector<ArenaBinds>>(std::move(pending_binds));
    pending_binds.clear();
    info.before_submit.push_back([this, binds, wait_tick, wait_sema, signal_tick, signal_sema] {
        std::vector<vk::SparseBufferMemoryBindInfo> buffer_binds;
        buffer_binds.reserve(binds->size());
        u64 ranges = 0;
        for (const auto& arena_binds : *binds) {
            ranges += arena_binds.binds.size();
            buffer_binds.emplace_back(vk::SparseBufferMemoryBindInfo{
                .buffer = arena_binds.arena->Handle(),
                .bindCount = static_cast<u32>(arena_binds.binds.size()),
                .pBinds = arena_binds.binds.data(),
            });
        }
        const vk::TimelineSemaphoreSubmitInfo timeline_si = {
            .waitSemaphoreValueCount = wait_tick ? 1u : 0u,
            .pWaitSemaphoreValues = wait_tick ? &wait_tick : nullptr,
            .signalSemaphoreValueCount = 1u,
            .pSignalSemaphoreValues = &signal_tick,
        };
        const vk::BindSparseInfo sparse_info = {
            .pNext = &timeline_si,
            .waitSemaphoreCount = wait_tick ? 1u : 0u,
            .pWaitSemaphores = wait_tick ? &wait_sema : nullptr,
            .bufferBindCount = static_cast<u32>(buffer_binds.size()),
            .pBufferBinds = buffer_binds.data(),
            .signalSemaphoreCount = 1u,
            .pSignalSemaphores = &signal_sema,
        };
        const auto bind_start = std::chrono::steady_clock::now();
        auto submit_result = instance.GetGraphicsQueue().bindSparse(sparse_info);
        const double bind_ms = std::chrono::duration<double, std::milli>(
                                   std::chrono::steady_clock::now() - bind_start)
                                   .count();
        arena_bind_calls.fetch_add(1, std::memory_order_relaxed);
        arena_bind_ranges.fetch_add(ranges, std::memory_order_relaxed);
        arena_bind_signaled.store(signal_tick, std::memory_order_relaxed);
        if (bind_ms >= 50.0) {
            std::printf("Guest memory: vkQueueBindSparse of %llu ranges took %.0f ms in the driver\n",
                        (unsigned long long)ranges, bind_ms);
        }
        if (submit_result == vk::Result::eErrorDeviceLost) {
            Vulkan::Breadcrumbs::ReportDeviceLost("submit");
        }
        ASSERT_MSG(submit_result != vk::Result::eErrorDeviceLost, "Device lost during submit");
    });
}

void SparseArena::ReportBinds() {
    const u64 signaled = arena_bind_signaled.load(std::memory_order_relaxed);
    const auto [result, done] =
        instance.GetDevice().getSemaphoreCounterValue(memory_semaphore.Handle());
    std::printf("Arena binds: %llu vkQueueBindSparse calls (%llu ranges); the last one signals %llu, "
                "the GPU is at %llu%s\n",
                (unsigned long long)arena_bind_calls.load(), (unsigned long long)arena_bind_ranges.load(),
                (unsigned long long)signaled,
                result == vk::Result::eSuccess ? (unsigned long long)done : 0ull,
                result == vk::Result::eSuccess && done < signaled
                    ? ": the submissions after it wait for a bind not done yet"
                    : "");
}

FastReadback::FastReadback(const Instance& instance, Scheduler& scheduler_)
    : scheduler{scheduler_}, device{instance.GetDevice()}, queue{instance.GetReadbackQueue()},
      allocator{instance.GetAllocator()} {
    if (!queue) {
        return;
    }
    pool = Vulkan::Check(device.createCommandPoolUnique({
        .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        .queueFamilyIndex = instance.GetReadbackQueueFamilyIndex(),
    }));
    cmdbuf = Vulkan::Check(device.allocateCommandBuffers({
        .commandPool = *pool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1,
    }))[0];
    fence = Vulkan::Check(device.createFenceUnique({}));
}

FastReadback::~FastReadback() {
    if (buffer) {
        vmaDestroyBuffer(allocator, buffer, allocation);
    }
}

const u8* FastReadback::Copy(const VideoCore::Buffer* arena,
                             std::span<const vk::BufferCopy> copies, u64 total, u64 tick) {
    const vk::Semaphore semaphore = scheduler.GetWorkSemaphore()->Handle();
    if (!Ensure(total)) {
        return nullptr;
    }
    Vulkan::Check(cmdbuf.reset());
    Vulkan::Check(cmdbuf.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit}));
    cmdbuf.copyBuffer(arena->Handle(), vk::Buffer{buffer}, copies);
    const vk::MemoryBarrier2 to_host{
        .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eHost,
        .dstAccessMask = vk::AccessFlagBits2::eHostRead,
    };
    cmdbuf.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &to_host});
    Vulkan::Check(cmdbuf.end());
    const vk::SemaphoreSubmitInfo wait{
        .semaphore = semaphore,
        .value = tick,
        .stageMask = vk::PipelineStageFlagBits2::eTransfer,
    };
    const vk::CommandBufferSubmitInfo command{.commandBuffer = cmdbuf};
    const vk::SubmitInfo2 submit{
        .waitSemaphoreInfoCount = 1,
        .pWaitSemaphoreInfos = &wait,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &command,
    };
    if (queue.submit2(submit, *fence) != vk::Result::eSuccess) {
        return nullptr;
    }
    const auto result = device.waitForFences(*fence, vk::True, UINT64_MAX);
    Vulkan::Check(device.resetFences(*fence));
    if (result != vk::Result::eSuccess) {
        return nullptr;
    }
    vmaInvalidateAllocation(allocator, allocation, 0, VK_WHOLE_SIZE);
    return mapped;
}

bool FastReadback::Ensure(u64 size) {
    if (size <= capacity) {
        return true;
    }
    if (buffer) {
        vmaDestroyBuffer(allocator, buffer, allocation);
        buffer = VK_NULL_HANDLE;
        capacity = 0;
    }
    const u64 new_capacity = std::max<u64>(std::bit_ceil(size), 1_MB);
    const VkBufferCreateInfo buffer_ci{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = new_capacity,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    const VmaAllocationCreateInfo alloc_ci{
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
    };
    VmaAllocationInfo info{};
    if (vmaCreateBuffer(allocator, &buffer_ci, &alloc_ci, &buffer, &allocation, &info) !=
        VK_SUCCESS) {
        buffer = VK_NULL_HANDLE;
        return false;
    }
    mapped = static_cast<u8*>(info.pMappedData);
    capacity = new_capacity;
    return true;
}

} // namespace Vulkan
