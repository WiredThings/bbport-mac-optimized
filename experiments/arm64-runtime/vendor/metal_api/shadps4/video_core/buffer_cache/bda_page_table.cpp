// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_core/buffer_cache/bda_page_table.h"
#include "common/assert.h"
#include "video_core/cache_backend.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
namespace VideoCore {
BdaPageTable::BdaPageTable(CacheBackend& backend_, u32 shift, bool dual, bool sparse)
    : backend{backend_}, two_level{sparse}, views{dual ? 2u : 1u},
      pages{Common::BdaTable::Pages(shift)},
      root_entries{sparse ? Common::BdaTable::RootEntries(shift) : pages},
      leaf_bytes{Common::BdaTable::LeafEntries * 8} {
    const u64 bytes = root_entries * 8 * views;
    root = std::make_unique<Buffer>(backend, 0, bytes, MemoryType::DeviceLocal, "BDA root table");
    if (!two_level) {
        backend.FillBuffer(root.get(), 0, bytes, 0);
        Report();
        return;
    }
    leaves.resize(root_entries);
    zero = std::make_unique<Buffer>(backend, 0, leaf_bytes * views, MemoryType::DeviceLocal,
                                    "BDA zero leaf");
    backend.FillBuffer(zero.get(), 0, zero->SizeBytes(), 0);
    backend.RegisterBdaLeaf(zero.get(), true);
    const auto staging = backend.StagingRequest(bytes, MemoryType::HostUncached);
    auto* pointers = reinterpret_cast<u64*>(staging.mapped);
    for (u32 view = 0; view < views; ++view)
        std::fill_n(pointers + view * root_entries, root_entries,
                    zero->BufferDeviceAddress() + view * leaf_bytes);
    staging.Flush();
    const vk::BufferCopy copy{staging.offset, 0, bytes};
    backend.CopyBuffer(staging.buffer, root.get(), std::span{&copy, 1});
    Report();
}
BdaPageTable::~BdaPageTable() {
    if (zero)
        backend.RegisterBdaLeaf(zero.get(), false);
    for (const auto& leaf : leaves)
        if (leaf)
            backend.RegisterBdaLeaf(leaf.get(), false);
    // UniqueBuffer destruction follows the backend's completed-tick retirement path.
}
u64 BdaPageTable::AllocatedBytes() const {
    return root->SizeBytes() + (zero ? zero->SizeBytes() : 0) + leaf_count * leaf_bytes * views;
}
Buffer& BdaPageTable::EnsureLeaf(u64 index, std::vector<u64>& created) {
    ASSERT(index < leaves.size());
    auto& leaf = leaves[index];
    if (!leaf) {
        leaf = std::make_unique<Buffer>(backend, 0, leaf_bytes * views, MemoryType::DeviceLocal,
                                        "BDA leaf");
        backend.FillBuffer(leaf.get(), 0, leaf->SizeBytes(), 0);
        backend.RegisterBdaLeaf(leaf.get(), true);
        ++leaf_count;
        created.push_back(index);
    }
    return *leaf;
}
void BdaPageTable::CopyPlane(const Buffer* source, std::span<const vk::BufferCopy> copies, u64 view,
                             std::vector<u64>& created) {
    for (const auto& copy : copies) {
        ASSERT(copy.dstOffset % 8 == 0 && copy.size % 8 == 0 && copy.dstOffset <= pages * 8 &&
               copy.size <= pages * 8 - copy.dstOffset);
        u64 at = copy.dstOffset, read = copy.srcOffset, remaining = copy.size;
        while (remaining) {
            const u64 leaf_index = at / leaf_bytes, offset = at % leaf_bytes;
            const u64 n = std::min(remaining, leaf_bytes - offset);
            auto& leaf = EnsureLeaf(leaf_index, created);
            const vk::BufferCopy part{read, view * leaf_bytes + offset, n};
            backend.CopyBuffer(source, &leaf, std::span{&part, 1});
            at += n;
            read += n;
            remaining -= n;
        }
    }
}
void BdaPageTable::Publish(std::span<const u64> created) {
    if (created.empty())
        return;
    const auto staging =
        backend.StagingRequest(created.size() * views * 8, MemoryType::HostUncached);
    auto* pointers = reinterpret_cast<u64*>(staging.mapped);
    std::vector<vk::BufferCopy> copies;
    copies.reserve(created.size() * views);
    u64 index = 0;
    for (const auto leaf : created)
        for (u32 view = 0; view < views; ++view) {
            pointers[index] = leaves[leaf]->BufferDeviceAddress() + view * leaf_bytes;
            copies.emplace_back(staging.offset + index * 8, (leaf + view * root_entries) * 8, 8);
            ++index;
        }
    staging.Flush();
    backend.CopyBuffer(staging.buffer, root.get(), copies);
}
void BdaPageTable::Copy(const Buffer* source, std::span<const vk::BufferCopy> current,
                        std::span<const vk::BufferCopy> guest) {
    ASSERT(views == 2 || guest.empty());
    if (current.empty() && guest.empty())
        return;
    if (!two_level) {
        std::vector<vk::BufferCopy> copies(current.begin(), current.end());
        for (auto part : guest) {
            part.dstOffset += pages * 8;
            copies.push_back(part);
        }
        backend.CopyBuffer(source, root.get(), copies);
        return;
    }
    // The bound root is the logical hazard anchor for indirect leaf accesses. End/barrier
    // earlier shader readers before overwriting leaves; mark it written for later bindings.
    if (backend.IsBufferAccessed(root.get(), 0, root->SizeBytes(), true))
        backend.FlushBarriers();
    std::vector<u64> created;
    CopyPlane(source, current, 0, created);
    CopyPlane(source, guest, 1, created);
    Publish(created); // zero/data initialization always precedes publishing a new pointer
    backend.AccessBuffer(root.get(), 0, root->SizeBytes(), vk::PipelineStageFlagBits2::eCopy,
                         vk::AccessFlagBits2::eTransferWrite);
}
void BdaPageTable::Report() {
    const auto now = std::chrono::steady_clock::now();
    if (now - reported < std::chrono::seconds(5))
        return;
    reported = now;
    std::printf("GPU BDA page table: mode=%s, allocated=%llu bytes, root=%llu bytes, leaves=%llu, "
                "views=%u, flat-equivalent=%llu bytes; stable leaves, no cache budget change\n",
                two_level ? "two-level" : "flat", (unsigned long long)AllocatedBytes(),
                (unsigned long long)root->SizeBytes(), (unsigned long long)leaf_count, views,
                (unsigned long long)(pages * 8 * views));
}
} // namespace VideoCore
