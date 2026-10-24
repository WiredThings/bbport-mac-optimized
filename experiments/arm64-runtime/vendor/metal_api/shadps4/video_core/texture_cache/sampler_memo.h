// SPDX-License-Identifier: GPL-3.0-or-later
// Adapted from Awakenchan/BloodBorneButPc, engine/bbhost-changes.patch (4f93643e).
#pragma once
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

namespace VideoCore {
// A stage-B-only fast path. The texture cache remains the owner and collector; every hit
// refreshes its LRU once per GC tick. Any collection invalidates all remembered references.
template <typename Ref, std::size_t Slots = 1024>
class SamplerMemo {
public:
    struct Key {
        std::array<std::uint64_t, 2> sharp{};
        std::uint64_t variant{}; // depth flag and exact extra-LOD-bias bits
        bool operator==(const Key&) const = default;
    };
    template <typename Lookup, typename Touch>
    Ref Get(const Key& key, std::uint64_t generation, std::uint64_t tick,
            Lookup&& lookup, Touch&& touch) {
        auto hash = (key.sharp[0] ^ std::rotl(key.sharp[1], 29) ^ key.variant) *
                    0x9E3779B97F4A7C15ull;
        hash ^= hash >> 32;
        auto& entry = entries[hash % Slots];
        if (entry.valid && entry.generation == generation && entry.key == key) {
            if (entry.touched_tick != tick) {
                touch(entry.lru_id);
                entry.touched_tick = tick;
            }
            return entry.ref;
        }
        std::size_t lru_id = 0;
        const Ref ref = lookup(lru_id);
        entry = {key, ref, lru_id, generation, tick, true};
        return ref;
    }
private:
    struct Entry {
        Key key{};
        Ref ref{};
        std::size_t lru_id{};
        std::uint64_t generation{}, touched_tick{};
        bool valid = false;
    };
    std::array<Entry, Slots> entries{};
};
} // namespace VideoCore
