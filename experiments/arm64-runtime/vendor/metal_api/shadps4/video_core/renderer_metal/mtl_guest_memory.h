// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the game's direct memory as Metal buffers, used where it is (plan D6).
//
// The runtime hands out direct memory in chunks (runtime_memory.c); with BB_GUEST_IN_PLACE (or
// BB_GUEST_GPU_MEMORY=1) the GPU library gets each chunk first. Here a chunk becomes an
// MTLBuffer over the runtime's backing view (newBufferWithBytesNoCopy, shared storage: Apple
// silicon has one memory), resident through the device's residency set, so shaders reach it
// through its gpuAddress and transfers through the buffer. The runtime keeps the memory in its
// memfd (CHUNK_IMPORTED) and never releases a chunk (released memory is cleared), so the buffers
// live as long as the process.
#pragma once

#include <cstdint>

namespace Metal {

class Device;

namespace GuestMemory {

struct Chunk {
    std::uint64_t phys = 0;         ///< first byte of direct memory it holds
    std::uint64_t size = 0;
    void* buffer = nullptr;         ///< id<MTLBuffer> (retained by this module)
    std::uint64_t gpu_address = 0;  ///< the buffer's gpuAddress
    std::uint8_t* host = nullptr;   ///< the chunk in the backing view
    std::uint32_t index = 0;        ///< creation order (stable key)
};

/// Hands the runtime's direct memory chunks to `device` from now on, when the memory model wants
/// the GPU to use guest memory in place. Before the runtime maps direct memory (the rasterizer's
/// constructor, as BbGuestMemory::Install on Vulkan).
void Install(Device& device);
/// Whether the memory model wants the GPU to use guest memory in place (BB_GUEST_IN_PLACE=1 or
/// BB_GUEST_GPU_MEMORY=1): Install then takes the runtime's chunks. Valid before Install.
bool Wanted();
/// Makes this module the one the shared caches ask about chunks (VideoCore::GuestChunks); before
/// the caches are made (Metal::Runtime's constructor).
void UseAsChunkProvider();
/// Whether Install handed the runtime's chunks to Metal.
bool Installed();
/// Metal allocation bytes aliasing CPU-owned guest backing (not additional RAM).
std::uint64_t ImportedBytes();
/// The chunk holding direct memory address `phys`, or null.
const Chunk* Find(std::uint64_t phys);


} // namespace GuestMemory
} // namespace Metal
