// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstdint>

namespace VideoCore {
// Recording-thread metadata only, like group_use/group_demoted. This memo never
// changes ranges or timestamps. Record only after the complete legacy operation;
// Invalidate whenever a demotion adds side effects that must be replayed.
class ResidencyUseMemo {
public:
    explicit ResidencyUseMemo(std::uint64_t initial_epoch = 1) : epoch{initial_epoch} {}
    std::uint64_t BeginSecond(std::uint32_t now) {
        if (!has_second || second != now) {
            Invalidate();
            second = now;
            has_second = true;
        }
        return epoch;
    }
    std::uint64_t Generation() const { return epoch; }
    void Invalidate() {
        if (++epoch == 0) {
            epoch = 1;
            for (auto& entry : entries) entry.valid = false;
        }
    }
    bool Seen(std::uint64_t first, std::uint64_t last) const {
        const auto& entry = entries[Slot(first, last)];
        return entry.valid && entry.epoch == epoch && entry.first == first && entry.last == last;
    }
    void Record(std::uint64_t first, std::uint64_t last, std::uint64_t completed_epoch) {
        // Do not memoize work whose operation observed an intervening invalidation.
        if (completed_epoch != epoch) return;
        entries[Slot(first, last)] = {first, last, epoch, true};
    }
private:
    static std::size_t Slot(std::uint64_t first, std::uint64_t last) {
        return ((first * 0x9e3779b97f4a7c15ull) ^ (last * 0xd6e8feb86659fd93ull)) >> 59;
    }
    struct Entry {
        std::uint64_t first = 0, last = 0, epoch = 0;
        bool valid = false;
    };
    std::array<Entry, 32> entries{};
    std::uint64_t epoch = 1;
    std::uint32_t second = 0;
    bool has_second = false;
};
} // namespace VideoCore
