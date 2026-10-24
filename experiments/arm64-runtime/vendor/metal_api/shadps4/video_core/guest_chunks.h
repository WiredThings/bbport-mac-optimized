// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the game's direct memory chunks as the shared caches see them, whichever backend made
// them (Vulkan: BbGuestMemory, shim/bbport_guest_memory.h; Metal: renderer_metal/mtl_guest_memory).
// The backend that owns the guest memory installs its provider before the caches are made;
// without one the Vulkan module answers (its queries are safe without a Vulkan device).
#pragma once

#include <optional>

#include "common/types.h"

namespace VideoCore::GuestChunks {

/// A chunk of direct memory [phys, phys + size) the GPU uses in place.
struct Chunk {
    u64 phys = 0;
    u64 size = 0;
    u32 index = 0;         ///< creation order (stable key)
    u64 memory_handle = 0; ///< the sparse arena's name for its memory (Vulkan only; else 0)
};

struct Provider {
    /// The chunk holding direct memory address `phys`; false if none.
    bool (*find)(u64 phys, Chunk& out);
    /// BB_LAYER_MEMORY: the layer's memory module binds the game's memory (see BbGuestMemory).
    bool (*layer_memory)();
    /// The chunks are imported host memory (the arena keeps its first bindings).
    bool (*host_imported)();
};

/// Called by a backend before its caches exist (Metal::Runtime's constructor).
void SetProvider(const Provider& provider);

std::optional<Chunk> Find(u64 phys);
bool LayerMemory();
bool HostImported();

} // namespace VideoCore::GuestChunks
