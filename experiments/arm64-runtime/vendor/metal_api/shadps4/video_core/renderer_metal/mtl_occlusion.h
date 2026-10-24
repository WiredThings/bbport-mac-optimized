// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <memory>
#include "video_core/gpu_services.h"
namespace VideoCore { class BufferCache; struct Buffer; }
namespace Metal {
class Device;
class Scheduler;
class Runtime;
// Native visibility counters with GPU-ordered PS4 DB-counter snapshots. No CPU readback,
// polling, background guest-memory writes or extra wait/flush on the event path.
class OcclusionQueries final : public VideoCore::OcclusionApi {
public:
    OcclusionQueries(Device& device, Scheduler& scheduler, Runtime& runtime,
                     VideoCore::BufferCache* cache = nullptr);
    ~OcclusionQueries() override;
    void Event(VAddr address, u32 pairs) override;
    void WriteBeforeLabel() override {} // each Event already encoded its snapshot in stream order
    u64 Events() const noexcept override { return events; }
    bool OnAccess(VAddr, u64, bool, bool) override { return false; }
    // Same resolve path, with an explicit destination for offscreen tests (no guest mappings).
    void Snapshot(const VideoCore::Buffer* destination, u64 offset, VAddr address, u32 pairs);
    void MarkUnmeasured();
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    u64 events = 0;
};
} // namespace Metal
