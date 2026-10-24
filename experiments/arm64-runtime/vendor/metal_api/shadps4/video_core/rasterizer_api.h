// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <string_view>
#include <fmt/format.h>

#include "common/types.h"
#include "bbport_toggles.h"
#include "video_core/renderer_vulkan/vk_draw_pipe.h"

namespace Vulkan {
// Draw preparation (vk_draw_prep.h) is part of the Vulkan backend for now: other backends return
// no DrawPreparation and are passed no PreparedDraw.
class DrawPreparation;
struct PreparedDraw;
} // namespace Vulkan

namespace VideoCore {

/// bbport: what the frontend (Liverpool, the memory manager and its hooks, the page fault
/// handlers) calls on a renderer backend's rasterizer. The methods keep the Vulkan backend's
/// names and contracts; vk_rasterizer.h documents each one. Selected at startup (BB_RENDERER).
class RasterizerApi {
public:
    virtual ~RasterizerApi() = default;

    /// Policy flags from the environment, the same for every backend.
    /// BB_HONEST_LABELS / the new memory model: fences are written when the GPU has finished the
    /// work before them.
    static bool HonestLabels();
    /// Whether ZPASS_DONE events go to OcclusionEvent (BB_OCCLUSION_QUERIES).
    static bool OcclusionTranslated();

    // Draws and dispatches (GPU command thread).
    virtual void Draw(bool is_indexed, u32 index_offset = 0,
                      const Vulkan::PreparedDraw* prepared = nullptr) = 0;
    virtual void DrawIndirect(bool is_indexed, VAddr arg_address, u32 offset, u32 size,
                              u32 max_count, VAddr count_address, u16 vertex_sgpr_offset,
                              u16 instance_sgpr_offset) = 0;
    virtual void DispatchDirect() = 0;
    virtual void DispatchIndirect(VAddr address, u32 offset, u32 size) = 0;
    /// Draw preparation workers fed and consumed by Liverpool; null when the backend has none.
    virtual Vulkan::DrawPreparation* GetDrawPreparation() = 0;

    // Debug markers.
    void ScopeMarker(fmt::string_view fmt, fmt::format_args args, auto&& func) {
        if (HostMarkersEnabled()) {
            ScopeMarkerBegin(fmt::vformat(fmt, args));
            func();
            ScopeMarkerEnd();
        } else {
            func();
        }
    }
    virtual bool HostMarkersEnabled() const = 0;
    virtual void ScopeMarkerBegin(const std::string_view& str, bool from_guest = false) = 0;
    virtual void ScopeMarkerEnd(bool from_guest = false) = 0;
    virtual void ScopedMarkerInsert(const std::string_view& str, bool from_guest = false) = 0;
    virtual void ScopedMarkerInsertColor(const std::string_view& str, const u32 color,
                                         bool from_guest = false) = 0;

    // DMA and GDS.
    virtual void FillBuffer(VAddr address, u32 num_bytes, u32 value, bool is_gds) = 0;
    virtual void CopyBuffer(VAddr dst, VAddr src, u32 num_bytes, bool dst_gds,
                            bool src_gds) = 0;
    virtual bool DmaMayWriteOnCpu(VAddr dst, u32 num_bytes) = 0;
    virtual u32 ReadDataFromGds(u32 gsd_offset) = 0;

    // Guest memory: mappings, CPU writes, faults.
    virtual bool InvalidateMemory(VAddr addr, u64 size, bool assume_locks = false) = 0;
    virtual void NoteAssetWrite(VAddr addr, u64 size) = 0;
    virtual void OnCpuWrite(VAddr addr, u64 size) = 0;
    virtual void NoteLateCommandWrite(VAddr addr, const void* data, u64 size) = 0;
    virtual void NoteCommandWrite(VAddr addr, const void* data, u64 size,
                                  bool recording_thread) = 0;
    virtual void NoteCommandWriteInOrder(VAddr addr, const void* data, u64 size) = 0;
    virtual void WaitForPendingReads(VAddr address, u64 size) = 0;
    virtual bool IsMapped(VAddr addr, u64 size) = 0;
    virtual void MapMemory(VAddr addr, u64 size) = 0;
    virtual void RegisterMemory(VAddr addr, u64 size) = 0;
    virtual void UnmapMemory(VAddr addr, u64 size) = 0;
    virtual bool ReadMemory(VAddr addr, u64 size, bool assume_locks = false) = 0;
    virtual void ProcessDownloadImages() = 0;
    virtual bool IsGpuSideThread() const = 0;
    virtual bool IsGpuSideThreadId(u32 tid) const = 0;
    virtual bool OnWriteFault(VAddr addr, bool assume_locks, u64 guest_rip = 0) = 0;
    virtual bool OnOcclusionPageAccess(VAddr addr, u64 rip, bool write, bool gpu_thread) = 0;
    virtual bool OnVramDataAccess(VAddr addr, bool assume_locks) = 0;

    // Ordering between the GPU command thread (stage A) and the draw recording thread.
    virtual void DrainDrawPipe(u32 reason = Vulkan::DrawPipe::ReasonRasterizer,
                               u32 line = __builtin_LINE(),
                               const char* function = __builtin_FUNCTION()) = 0;
    using OrderedTask = void (*)(RasterizerApi& rasterizer, const u8* data);
    virtual bool RunInOrder(OrderedTask task, const void* data, u32 size,
                            u64 toggle = BbToggle::PipelinedTasks,
                            bool reads_guest_memory = true) = 0;
    virtual u64 DrawPipeHead() const = 0;
    virtual bool DrawPipeReached(u64 position) const = 0;
    virtual bool DrawPipeIdle() const = 0;
    virtual void RetireSubmission() = 0;
    virtual void NotePendingGpuWrite(VAddr address, u64 size) = 0;
    virtual void NotePendingRead(VAddr address, u64 size) = 0;

    // Labels, fences, host copies, submissions.
    virtual void WaitHostCopies() = 0;
    virtual void WaitHostCopiesFor(VAddr address, u64 size) = 0;
    virtual void WaitDeferredSignals() = 0;
    virtual void SignalAfterHostCopies(std::function<void()> signal) = 0;
    virtual void SignalAfterGpu(std::function<void()> signal, VAddr label = 0, u64 value = 0) = 0;
    virtual bool HasUnsubmittedSignals() = 0;
    virtual bool WriteDataOnGpu(VAddr address, const void* data, u32 size) = 0;
    virtual bool WriteLabelOnGpu(VAddr address, u64 value, u32 num_bytes) = 0;
    virtual bool WriteTimestampOnGpu(VAddr address, bool end_of_pipe) = 0;
    virtual bool OcclusionEvent(VAddr address, u32 pairs) = 0;
    virtual u64 OcclusionEvents() const = 0;
    virtual void SubmitForSignals() = 0;
    virtual bool PipeSignals() const = 0;
    virtual bool SignalsPublished(u64 position) const = 0;
    virtual void RequestSignalFlush() = 0;
    virtual bool PendingSignalValue(VAddr address, u64& value) = 0;
    virtual u64 CurrentTick() const = 0;
    virtual u64 Flush() = 0;
    virtual void Finish() = 0;
    virtual void OnSubmit() = 0;
};

} // namespace VideoCore
