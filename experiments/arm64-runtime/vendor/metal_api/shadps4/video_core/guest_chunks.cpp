// SPDX-License-Identifier: GPL-2.0-or-later

#include <atomic>

#include "bbport_guest_memory.h"
#include "video_core/guest_chunks.h"

namespace VideoCore::GuestChunks {

namespace {

bool VulkanFind(u64 phys, Chunk& out) {
    const BbGuestMemory::Chunk* chunk = BbGuestMemory::Find(phys);
    if (!chunk) {
        return false;
    }
    out = {chunk->phys, chunk->size, chunk->index, chunk->MemoryHandle()};
    return true;
}

constexpr Provider VulkanProvider{&VulkanFind, &BbGuestMemory::LayerMemory,
                                  &BbGuestMemory::HostImported};
std::atomic<const Provider*> active{&VulkanProvider};
Provider installed{};

} // namespace

void SetProvider(const Provider& provider) {
    installed = provider;
    active.store(&installed, std::memory_order_release);
}

std::optional<Chunk> Find(u64 phys) {
    Chunk chunk;
    if (!active.load(std::memory_order_acquire)->find(phys, chunk)) {
        return std::nullopt;
    }
    return chunk;
}

bool LayerMemory() {
    return active.load(std::memory_order_acquire)->layer_memory();
}

bool HostImported() {
    return active.load(std::memory_order_acquire)->host_imported();
}

} // namespace VideoCore::GuestChunks
