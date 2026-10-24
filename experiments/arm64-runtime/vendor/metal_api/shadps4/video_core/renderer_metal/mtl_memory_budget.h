// SPDX-License-Identifier: GPL-2.0-or-later
// Adapted from bmy/bbport-mac a923b61 + 4de5fd1: count only memory outside
// the macOS graphics ledger against the process's unified-memory allowance.
#pragma once
#include <cstdint>

namespace Metal::MemoryBudget {
struct ProcessMemory {
    std::uint64_t outside_graphics{};
    std::uint64_t graphics{};
    bool valid{};
};

inline ProcessMemory FromLedger(std::uint64_t footprint, std::int64_t graphics) {
    const auto bytes = graphics > 0 ? std::uint64_t(graphics) : 0;
    return {footprint > bytes ? footprint - bytes : 0, bytes, true};
}

// currentAllocatedSize includes no-copy wrappers over CPU-owned guest pages.
// Those pages already occur in outside_graphics: do not charge them twice.
// Match bmy's max(renderer usage, graphics ledger), after removing the alias.
inline std::uint64_t GraphicsUsage(std::uint64_t allocated, std::uint64_t imported,
                                   const ProcessMemory& process) {
    if (!process.valid) return allocated;
    const auto owned = allocated > imported ? allocated - imported : 0;
    return owned > process.graphics ? owned : process.graphics;
}

inline std::uint64_t Unified(std::uint64_t limit, const ProcessMemory& process) {
    if (!process.valid) return limit;
    constexpr std::uint64_t Floor = std::uint64_t{2} << 30;
    const auto floor = limit < Floor ? limit : Floor;
    const auto remaining = limit > process.outside_graphics ? limit - process.outside_graphics : 0;
    return remaining > floor ? remaining : floor;
}
} // namespace Metal::MemoryBudget
