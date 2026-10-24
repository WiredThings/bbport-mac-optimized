// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace BbStagingRecent {
struct Selection { std::size_t index; std::uint64_t offset; };
inline constexpr std::size_t MaxOrderedBlocks = 64;
// The callback remains the sole reservation/fence authority. Ordering never
// treats age or a cached completion bit as permission to overwrite GPU data.
// Try current first; then newest use first, preserving cyclic order on ties.
// Bound stack/work for unusually large pools by using the original cyclic order.
template<class Blocks, class Reserve>
std::optional<Selection> Try(const Blocks& blocks, std::size_t current, Reserve&& reserve) {
    const auto n=blocks.size();
    if (!n || current>=n) return std::nullopt;
    if (n>MaxOrderedBlocks) {
        for (std::size_t k=0;k<n;++k) {
            const auto i=(current+k)%n;
            if (auto offset=reserve(i)) return Selection{i,*offset};
        }
        return std::nullopt;
    }
    std::array<std::size_t,MaxOrderedBlocks> order{};
    for (std::size_t k=0;k<n;++k) order[k]=(current+k)%n;
    for (std::size_t k=2;k<n;++k) {
        const auto index=order[k];auto at=k;
        while (at>1 && blocks[index].last_used_frame>blocks[order[at-1]].last_used_frame) {
            order[at]=order[at-1];--at;
        }
        order[at]=index;
    }
    for (std::size_t k=0;k<n;++k) {
        const auto i=order[k];
        if (auto offset=reserve(i)) return Selection{i,*offset};
    }
    return std::nullopt;
}
}
