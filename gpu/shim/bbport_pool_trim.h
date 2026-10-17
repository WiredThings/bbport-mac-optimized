// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// Compaction used by bmy/bbport-mac d6704c6. Never retire a resource whose
// submission has not completed; the returned handles may then be freed by Vulkan.
template <class Handle>
std::vector<Handle> BbTrimCompletedBuffers(std::vector<std::uint64_t>& ticks,
                                          std::vector<Handle>& buffers,
                                          std::uint64_t gpu_tick, std::size_t keep_idle) {
    std::vector<Handle> freed;
    std::size_t kept_idle = 0, kept = 0;
    for (std::size_t i = 0; i < ticks.size(); ++i) {
        const bool idle = gpu_tick >= ticks[i];
        if (idle && kept_idle >= keep_idle) {
            freed.push_back(buffers[i]);
            continue;
        }
        kept_idle += idle ? 1 : 0;
        ticks[kept] = ticks[i];
        buffers[kept++] = buffers[i];
    }
    ticks.resize(kept);
    buffers.resize(kept);
    return freed;
}
