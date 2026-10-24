// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <chrono>
#include <memory>
#include <span>
#include <vector>
#include "common/bda_page_table.h"
#include "video_core/buffer_cache/buffer.h"
namespace VideoCore {
class CacheBackend;
// One stable root, a shared zero leaf and lazily allocated leaves. Empty entries are
// reset on unmap; leaf buffers stay stable until shutdown (no stale pointers or churn).
class BdaPageTable {
public:
    BdaPageTable(CacheBackend&,u32 page_shift,bool dual_view,bool two_level);
    ~BdaPageTable();
    Buffer* Root() const {return root.get();}
    u64 GuestOffset() const {return root_entries;}
    u64 AllocatedBytes() const;
    u64 LeafCount() const {return leaf_count;}
    bool TwoLevel() const {return two_level;}
    // Offsets in both copy lists are logical byte offsets within one flat view.
    void Copy(const Buffer* source,std::span<const vk::BufferCopy> current,
              std::span<const vk::BufferCopy> guest={});
    void Report();
private:
    Buffer& EnsureLeaf(u64 index,std::vector<u64>& created);
    void CopyPlane(const Buffer*,std::span<const vk::BufferCopy>,u64 view,std::vector<u64>&);
    void Publish(std::span<const u64> created);
    CacheBackend& backend;
    bool two_level;
    u32 views;
    u64 pages,root_entries,leaf_bytes;
    std::unique_ptr<Buffer> root,zero;
    std::vector<std::unique_ptr<Buffer>> leaves;
    u64 leaf_count=0;
    std::chrono::steady_clock::time_point reported{};
};
} // namespace VideoCore
