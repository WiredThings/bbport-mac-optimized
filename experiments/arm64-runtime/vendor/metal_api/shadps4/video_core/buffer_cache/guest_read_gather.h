// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <vector>
#include "video_core/cache_backend.h"

namespace VideoCore {

struct GuestReadPiece {
    const Buffer* buffer{};
    u64 offset{};
    u64 size{};
};

// One draw/dispatch resource-preparation scope only, never a cross-draw or cross-frame cache.
struct GuestReadEpoch {
    u64 packet{}, tick{}, wrap{}, cpu{}, mapping{}, layer{}, gpu{};
    bool operator==(const GuestReadEpoch&) const = default;
};

class GuestReadSnapshots {
public:
    static constexpr size_t Capacity = 16;
    void Clear() { count = next = 0; }

    std::optional<std::pair<const Buffer*, u64>> Find(
        const StreamBuffer& ring, VAddr address, u64 size, u64 alignment,
        const GuestReadEpoch& now) {
        if (!(now == epoch)) { Clear(); epoch = now; }
        if (!now.gpu || (now.mapping & 1) || !size) return std::nullopt;
        for (size_t i = 0; i < count; ++i) {
            const auto& entry = entries[i];
            if (address < entry.address) continue;
            const u64 delta = address - entry.address;
            if (delta <= entry.size && size <= entry.size - delta &&
                (!alignment || (entry.offset + delta) % alignment == 0))
                return std::pair<const Buffer*, u64>{&ring, entry.offset + delta};
        }
        return std::nullopt;
    }

    // Called only after an unformatted gather whose ONLY recorded writes target the stream
    // ring itself. Those copies advance the backend's global write epoch; earlier snapshots
    // remain valid. Any other write between gathers invalidates them on the next Find.
    void Remember(VAddr address, u64 size, u64 offset,
                  const GuestReadEpoch& before, const GuestReadEpoch& after) {
        if (!before.gpu || before.packet != after.packet || before.cpu != after.cpu ||
            before.mapping != after.mapping || (after.mapping & 1) || before.layer != after.layer) {
            Clear(); return;
        }
        if (!(epoch == before) || before.tick != after.tick || before.wrap != after.wrap) Clear();
        epoch = after;
        entries[next] = {address, size, offset};
        next = (next + 1) % Capacity;
        count = std::min(count + 1, Capacity);
    }

private:
    struct Entry { VAddr address; u64 size, offset; };
    std::array<Entry, Capacity> entries{};
    size_t count = 0, next = 0;
    GuestReadEpoch epoch{};
};
static_assert(sizeof(GuestReadSnapshots) < 512); // CPU metadata only; no resource ownership.

// A rare read spanning guest chunks cannot use a sparse VA arena on Metal. Gather an exact
// snapshot in the existing stream ring instead. No persistent allocation or address-table
// entry is added. Copies are GPU commands, so earlier GPU writes and subsequent readers retain
// their order; the ring's normal tick watches prevent reuse while those readers are in flight.
// Resolve and validate the WHOLE range before reserving or recording anything. Never truncate
// the resource, substitute zeroes, or use this snapshot as a writable guest alias.
template <typename Resolve, typename BeforeCopy>
std::optional<std::pair<const Buffer*, u64>> GatherGuestRead(
    CacheBackend& backend, StreamBuffer& stream, VAddr address, u64 size,
    Resolve&& resolve, BeforeCopy&& before_copy) {
    if (!size || size > stream.SizeBytes() || address > std::numeric_limits<u64>::max() - size) {
        return std::nullopt;
    }
    struct Part { GuestReadPiece source; VAddr address; u64 destination; };
    std::vector<Part> parts;
    for (u64 done = 0; done < size;) {
        // Bound host metadata even for fragmented mappings. Rejection has no side effects.
        if (parts.size() == 4096) return std::nullopt;
        const auto piece = resolve(address + done, size - done);
        if (!piece || !piece->buffer || !piece->size || piece->size > size - done ||
            piece->offset > piece->buffer->SizeBytes() ||
            piece->size > piece->buffer->SizeBytes() - piece->offset) {
            return std::nullopt;
        }
        parts.push_back({*piece, address + done, done});
        done += piece->size;
    }
    const auto offset = stream.Reserve(size, std::max(backend.Limits().uniform_min_alignment,
                                                   backend.Limits().storage_min_alignment));
    if (!offset) return std::nullopt;
    for (const auto& part : parts) {
        before_copy(part.source, part.address);
        const vk::BufferCopy copy{part.source.offset, *offset + part.destination, part.source.size};
        backend.CopyBuffer(part.source.buffer, &stream, std::span{&copy, 1});
    }
    return std::pair<const Buffer*, u64>{&stream, *offset};
}

} // namespace VideoCore
