// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/div_ceil.h"
#include "video_core/buffer_cache/buffer_cache.h"
#include "video_core/buffer_cache/fault_manager.h"
#include "video_core/cache_backend.h"

namespace VideoCore {

static constexpr size_t MaxPageFaults = 1024;
static constexpr size_t PageFaultAreaSize = MaxPageFaults * sizeof(u64);

FaultManager::FaultManager(CacheBackend& backend_, BufferCache& buffer_cache_,
                           u32 sparse_pagebits, u64 sparse_num_pages_)
    : backend{backend_}, buffer_cache{buffer_cache_}, sparse_pagesize{1ULL << sparse_pagebits},
      sparse_num_pages{sparse_num_pages_}, fault_buffer_size{sparse_num_pages_ / 8},
      fault_buffer{backend_, 0, fault_buffer_size, MemoryType::DeviceLocal, "Fault Buffer"},
      download_buffer{backend_, 0, MaxPendingFaults * PageFaultAreaSize, MemoryType::HostCached} {
    backend.CreateFaultParser(sparse_pagebits, MaxPageFaults);
}

void FaultManager::ProcessFaultBuffer() {
    if (u64 wait_tick = fault_areas[current_area]) {
        backend.Wait(wait_tick);
        backend.PopPendingOperations();
    }

    const u32 offset = current_area * PageFaultAreaSize;
    u8* mapped = download_buffer.mapped_data.data() + offset;
    std::memset(mapped, 0, PageFaultAreaSize);

    // 1 bit per page, 32 pages per workgroup
    const u32 num_threads = sparse_num_pages / 32;
    const u32 num_workgroups = Common::DivCeil(num_threads, 64u);
    backend.ParseFaultBuffer({
        .fault_buffer = &fault_buffer,
        .fault_buffer_size = fault_buffer_size,
        .download =
            {
                .buffer = &download_buffer,
                .offset = offset,
                .range = PageFaultAreaSize,
            },
        .group_count_x = num_workgroups,
    });

    backend.DeferOperation([this, mapped, area = current_area] {
        fault_ranges.Clear();
        const u64* fault_buf = std::bit_cast<const u64*>(mapped);
        const u32 fault_count = fault_buf[0];
        for (u32 i = 1; i <= fault_count; ++i) {
            fault_ranges.Add(fault_buf[i], sparse_pagesize);
            LOG_INFO(Render_Vulkan, "Accessed non-GPU cached memory at {:#x}", fault_buf[i]);
        }
        fault_ranges.ForEach([&](VAddr start, VAddr end) {
            ASSERT_MSG((end - start) <= std::numeric_limits<u32>::max(),
                       "Buffer size is too large");
            (void)buffer_cache.ObtainBuffer(start, end - start, false);
        });
        fault_areas[area] = 0;
    });

    fault_areas[current_area++] = backend.CurrentTick();
    current_area %= MaxPendingFaults;
}

} // namespace VideoCore
