// SPDX-License-Identifier: GPL-2.0-or-later
// Native Metal adaptation of IFreemz's game velocity mirror (149f63645bbb).
#pragma once
#include <memory>
#include "video_core/render_state.h"
namespace Metal {
class Device;
class Scheduler;
class SceneTargets;
class RenderList;
class VelocityMirror final {
public:
    static constexpr u32 MaxBatchDraws = 128;
    VelocityMirror(Device&, Scheduler&, SceneTargets&, bool enabled);
    ~VelocityMirror();
    bool Enabled() const;
    static int PackingBinding(u64 fragment_hash);
    void FrameStart();
    // GPU copy only: guest constants may still be in flight. No CPU read or GPU-completion wait.
    bool Capture(const void* buffer, u64 offset, u64 size);
    // Seed private depth before the first guest velocity draw writes depth.
    bool Prepare(const VideoCore::RenderState&, u32 width, u32 height,
                 VideoCore::RenderState& mirror, u64 source_uid = 0);
    void InvalidateDepthSeed();
    void Drawn();
    RenderList& Record(const VideoCore::RenderState& mirror);
    void QueuedDraw();
    u32 PendingDraws() const;
    u64 BatchCount() const;
    void Flush();
    void Merge(const void* motion, u32 width, u32 height);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace Metal
