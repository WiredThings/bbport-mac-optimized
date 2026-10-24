// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <span>
#include <vector>
#include "video_core/amdgpu/resource.h"

namespace VideoCore {
// Intervals for residency-use bookkeeping, not execution commands. Original GPU copies,
// write clocks, descriptor bounds, page publication, image invalidation and barriers stay intact.
struct CopyRingRange {
    VAddr address = 0;
    u64 size = 0;
};
inline bool CopyRingBookkeepingValid(CopyRingRange declared, std::span<const CopyRingRange> ranges) {
    const u64 limit = u64{1} << 40;
    if (ranges.empty() || declared.address >= limit || declared.size > limit - declared.address) {
        return false;
    }
    VAddr previous_end = declared.address;
    for (const auto& range : ranges) {
        if (!range.size || range.address < previous_end || range.address < declared.address ||
            range.address - declared.address > declared.size ||
            range.size > declared.size - (range.address - declared.address)) return false;
        previous_end = range.address + range.size;
    }
    return true;
}
inline void MergeCopyRingRanges(std::vector<CopyRingRange>& ranges) {
    std::sort(ranges.begin(), ranges.end(), [](auto a, auto b) { return a.address < b.address; });
    size_t count = 0;
    for (const auto range : ranges) {
        if (count && range.address <= ranges[count-1].address + ranges[count-1].size) {
            auto& last = ranges[count-1];
            last.size = std::max(last.address + last.size, range.address + range.size) - last.address;
        } else ranges[count++] = range;
    }
    ranges.resize(count);
}
// The input is validated, sorted and disjoint. Emit each touched page/group once,
// without retaining another bitmap or changing the existing accounting granularity.
template <typename F>
inline void ForEachCopyRingPageRange(std::span<const CopyRingRange> ranges, u32 shift, F&& emit) {
    const u64 mask = (u64{1} << shift) - 1;
    VAddr low = 0, high = 0;
    for (auto range : ranges) {
        const VAddr a = range.address & ~mask;
        const VAddr b = (range.address + range.size + mask) & ~mask;
        if (high && a > high) { emit(low, high - low); low = a; }
        else if (!high) low = a;
        high = std::max(high, b);
    }
    if (high) emit(low, high - low);
}
struct CopyRingScope {
    CopyRingRange source, destination;
    std::vector<CopyRingRange> sources, destinations;
};
struct CopyRingGrid {
    u32 x, y, z, start_x, start_y, start_z;
    u32 threads_x, threads_y, threads_z;
    bool partial;
};
inline bool CopyRingLayout(const AmdGpu::Buffer& b, bool control) {
    using namespace AmdGpu;
    const auto format = control ? DataFormat::Format32_32_32 : DataFormat::Format32;
    const u64 size = u64(b.stride) * b.num_records;
    return b.Valid() && b.base_address && !(b.base_address & 3) &&
           !b.swizzle_enable && !b.add_tid_enable && b.GetDataFmt() == format &&
           (b.GetNumberFmt() == NumberFormat::Float || b.GetNumberFmt() == NumberFormat::Uint ||
            b.GetNumberFmt() == NumberFormat::Sint) && TryMapNumberConversion(NumberFormat(b.num_format), DataFormat(b.data_format)) ==
                std::optional{NumberConversion::None} &&
           b.dst_sel_x == u32(CompSwizzle::Red) &&
           (!control || (b.dst_sel_y == u32(CompSwizzle::Green) &&
                         b.dst_sel_z == u32(CompSwizzle::Blue))) &&
           b.stride == (control ? 12 : 4) && size && size <= 0xffffffffull &&
           size == b.GetSize() && size <= (u64{1} << 40) - b.base_address;
}
inline std::optional<CopyRingScope> BuildCopyRingScope(
    std::span<const u8> snapshot, const std::array<AmdGpu::Buffer, 3>& buffers,
    const CopyRingGrid& grid) {
    if (!grid.x || grid.x > 4096 || grid.y != 1 || grid.z != 1 || grid.start_x ||
        grid.start_y || grid.start_z || !grid.threads_x || grid.threads_x > 1024 ||
        grid.threads_y != 1 || grid.threads_z != 1 || grid.partial ||
        !CopyRingLayout(buffers[0], true) || !CopyRingLayout(buffers[1], false) ||
        !CopyRingLayout(buffers[2], false) || u64(grid.x) * 12 > snapshot.size() ||
        snapshot.size() > buffers[0].GetSize()) {
        return std::nullopt;
    }
    CopyRingScope result;
    result.sources.reserve(grid.x);
    result.destinations.reserve(grid.x);
    u64 low_src = ~u64{0}, low_dst = ~u64{0}, high_src = 0, high_dst = 0;
    for (u32 i = 0; i < grid.x; ++i) {
        std::array<u32, 3> entry;
        std::memcpy(entry.data(), snapshot.data() + u64(i) * 12, 12);
        // Include every index permitted by the control, even beyond the current
        // thread count. This is conservative if pipeline thread sizing differs.
        const u64 count = u64(entry[2]) + 1;
        const u64 src_end = u64(entry[1]) + count, dst_end = u64(entry[0]) + count;
        // Generated paged offsets are 32-bit byte offsets. Reject wrap rather than
        // narrowing around an address which the actual shader could wrap past.
        if (src_end > (u64{1} << 30) || dst_end > (u64{1} << 30) ||
            src_end * 4 > buffers[1].GetSize() || dst_end * 4 > buffers[2].GetSize()) {
            return std::nullopt;
        }
        result.sources.push_back({buffers[1].base_address + u64(entry[1]) * 4, count * 4});
        result.destinations.push_back({buffers[2].base_address + u64(entry[0]) * 4, count * 4});
        low_src = std::min(low_src, u64(entry[1]) * 4);
        low_dst = std::min(low_dst, u64(entry[0]) * 4);
        high_src = std::max(high_src, src_end * 4);
        high_dst = std::max(high_dst, dst_end * 4);
    }
    result.source = {buffers[1].base_address + low_src, high_src - low_src};
    result.destination = {buffers[2].base_address + low_dst, high_dst - low_dst};
    MergeCopyRingRanges(result.sources);
    MergeCopyRingRanges(result.destinations);
    return result;
}
} // namespace VideoCore
