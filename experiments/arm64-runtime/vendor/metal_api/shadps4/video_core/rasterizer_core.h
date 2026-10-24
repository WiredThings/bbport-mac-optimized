// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project

#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <span>
#include <thread>
#include "common/recursive_lock.h"
#include "common/shared_first_mutex.h"
#include "video_core/buffer_cache/buffer_cache.h"
#include "video_core/page_manager.h"
#include "video_core/pipeline_cache_core_types.h"
#include "video_core/scene_passes.h"
#include "video_core/rasterizer_api.h"
#include "video_core/bind_helper.h"
#include "video_core/constant_ring.h"
#include "video_core/renderer_vulkan/vk_draw_pipe.h"
#include "video_core/renderer_vulkan/vk_draw_prep.h"
#include "video_core/pipeline_cache_core.h"
#include "video_core/cache_backend.h"
#include "video_core/gpu_queue.h"
#include "video_core/gpu_services.h"
#include "video_core/render_state.h"
#include "video_core/texture_cache/texture_cache.h"
#include "video_core/texture_cache/sampler_memo.h"
#include <magic_enum/magic_enum.hpp>
#include "video_core/amdgpu/liverpool.h"

namespace AmdGpu {
struct Liverpool;
}

namespace Core {
class MemoryManager;
}

namespace VideoCore {

/// bbport: the backend-neutral half of a backend's rasterizer (plan P4.1, D2,
/// docs/metal/rasterizer_core_design.md): the stage A/B draw pipeline protocol, signals, labels
/// and fences, guest memory writes and page faults, memory notes, draw filtering, scene-resolution
/// decisions, resource selection and the bind helper. It owns the shared caches and the page
/// manager. The backend derives from it and implements what records commands: the protected pure
/// virtual hooks below, called 1:1 from the neutral code.
/// It reaches the GPU through the hooks, the CacheBackend and the GpuQueue (R6).
class RasterizerCore : public RasterizerApi {
public:
    explicit RasterizerCore(VideoCore::CacheBackend& backend, VideoCore::GpuQueue& queue,
                            AmdGpu::Liverpool* liverpool);
    ~RasterizerCore() override;

    [[nodiscard]] VideoCore::CacheBackend& GetBackend() noexcept {
        return backend;
    }

    [[nodiscard]] VideoCore::BufferCache& GetBufferCache() noexcept {
        return buffer_cache;
    }

    [[nodiscard]] VideoCore::TextureCache& GetTextureCache() noexcept {
        return texture_cache;
    }

    /// bbport: draw preparation workers (vk_draw_prep.h), fed and consumed by Liverpool.
    Vulkan::DrawPreparation* GetDrawPreparation() override {
        return draw_prep.get();
    }

    bool HostMarkersEnabled() const override {
        return host_markers_enabled;
    }

    void FillBuffer(VAddr address, u32 num_bytes, u32 value, bool is_gds) override;
    void CopyBuffer(VAddr dst, VAddr src, u32 num_bytes, bool dst_gds, bool src_gds) override;
    /// bbport: whether FillBuffer/CopyBuffer into guest memory at `dst` may write it on the CPU
    /// now (otherwise the GPU writes it, in stream order).
    [[nodiscard]] bool DmaMayWriteOnCpu(VAddr dst, u32 num_bytes) override;
    u32 ReadDataFromGds(u32 gsd_offset) override;
    bool InvalidateMemory(VAddr addr, u64 size, bool assume_locks = false) override;
    /// bbport: data written into GPU memory by a path we hear of (file reads, the game's resource
    /// loaders, DMA on the CPU): invalidated, and an asset for the buffer cache.
    void NoteAssetWrite(VAddr addr, u64 size) override;
    /// bbport: the CPU has written [addr, addr + size): invalidated, its bytes kept over GPU data that
    /// is read back first.
    void InvalidateAfterWrite(VAddr addr, u64 size);
    /// bbport (any thread): the command processor wrote these bytes outside the command stream
    /// (labels, timestamps, occlusion results): VRAM copies of them get them too.
    void NoteLateCommandWrite(VAddr addr, const void* data, u64 size) override {
        buffer_cache.NoteLateCommandWrite(addr, data, size);
    }
    /// bbport: the command processor wrote `size` bytes of `data` at addr with the CPU in decode order
    /// (WriteData, a constant RAM dump; the decoder reads them there). Where the GPU's own data there
    /// is in VRAM the bytes go into it in stream order, else the caches are invalidated (no write
    /// tracking). `recording_thread`: the draw recording thread, which owns the caches (no drain).
    void NoteCommandWrite(VAddr addr, const void* data, u64 size,
                          bool recording_thread) override;
    /// bbport (stage A, BB_CONSTRAM_PIPE, default on): before the command processor writes guest
    /// memory with the CPU (a constant RAM dump), waits until the recording thread has run the
    /// queued packets that read those bytes (their buffer bindings, DMA), not all of them.
    void WaitForPendingReads(VAddr address, u64 size) override;
    /// NoteCommandWrite for a write made on stage A: handed to the recording thread in order (a copy
    /// of the bytes) instead of draining it first.
    void NoteCommandWriteInOrder(VAddr addr, const void* data, u64 size) override;
    /// bbport: a guest thread wrote [addr, addr + size) with a libc import (memcpy, memset, memmove)
    /// over pages the caches watch (no write tracking): what a write fault did.
    void OnCpuWrite(VAddr addr, u64 size) override;
    /// GPU thread, before a write the guest can observe (see GpuQueue::WaitHostCopies).
    void WaitHostCopies() override {
        DrainDrawPipe();
        queue.WaitHostCopies();
    }
    /// Before a write into [address, address + size): waits only when a host copy may still read
    /// it (GpuQueue::WaitHostCopiesFor). BB_HOST_COPY_WAITS=all: always (the old behaviour).
    void WaitHostCopiesFor(VAddr address, u64 size) override {
        DrainDrawPipe();
        static const bool all = [] {
            const char* env = std::getenv("BB_HOST_COPY_WAITS");
            return env && std::string_view{env} == "all";
        }();
        if (all) {
            queue.WaitHostCopies();
        } else {
            queue.WaitHostCopiesFor(address, size);
        }
    }
    /// Notes the guest memory a host copy issued now reads (see
    /// CacheBackend::NoteHostCopySource).
    void NoteHostCopySource(VAddr address, u64 size) {
        backend.NoteHostCopySource(address, size);
    }

    /// bbport: GPU command thread: waits until the draw recording thread has recorded every
    /// draw handed to it (vk_draw_pipe.h); no-op on other threads.
    /// `line`/`function`: the caller, for the statistics of where stage A waits.
    void DrainDrawPipe(u32 reason = Vulkan::DrawPipe::ReasonRasterizer, u32 line = __builtin_LINE(),
                       const char* function = __builtin_FUNCTION()) override;
    /// Runs `task(rasterizer, copy of data)` in order with the draws: on the draw recording
    /// thread while the draw pipeline is in use (PipelinedTasks), else here after a drain.
    /// Returns true when the task was handed to the recording thread (not run yet).
    /// `reads_guest_memory`: the task may read guest memory (a CPU write on stage A over memory
    /// waits for it, WaitForPendingReads); false for fences, flips and signals.
    bool RunInOrder(OrderedTask task, const void* data, u32 size,
                    u64 toggle = BbToggle::PipelinedTasks,
                    bool reads_guest_memory = true) override;
    /// Stage A: the draw pipe position after the last handed-over packet, and whether the
    /// recording thread has run everything before a position.
    [[nodiscard]] u64 DrawPipeHead() const override {
        return draw_pipe ? draw_pipe->Head() : 0;
    }
    [[nodiscard]] bool DrawPipeReached(u64 position) const override {
        return !draw_pipe || draw_pipe->Reached(position);
    }
    /// Stage A: no draws waiting for the recording thread.
    [[nodiscard]] bool DrawPipeIdle() const override {
        return !draw_pipe || draw_pipe->Idle();
    }
    /// Stage A, end of a submission: its prepared draws stay alive until the recording thread
    /// has recorded them (instead of a drain).
    void RetireSubmission() override;
    /// Stage A: guest memory the recording thread will write for work handed to it (storage
    /// buffers, DMA, WriteData, fences); constants overlapping it are bound there, not copied here.
    void NotePendingGpuWrite(VAddr address, u64 size) override;
    /// Stage A: guest memory the recording thread will read for the next packet (buffer bindings).
    void NotePendingRead(VAddr address, u64 size) override;
    /// Before a guest-visible write: fences deferred earlier are written first.
    void WaitDeferredSignals() override {
        queue.WaitDeferredSignals();
    }
    /// Runs `signal` after the guest memory copies issued so far, without waiting here.
    void SignalAfterHostCopies(std::function<void()> signal) override {
        queue.SignalAfterHostCopies(std::move(signal));
    }
    /// bbport BB_HONEST_LABELS=1: runs `signal` once the GPU has finished every command recorded
    /// before this call (the submission of the current tick), in call order, on a thread that
    /// waits on the work semaphore: fences the guest sees when the work is done, not when it is
    /// recorded. `label` and `value`: the fence it writes (PendingSignalValue).
    void SignalAfterGpu(std::function<void()> signal, VAddr label = 0, u64 value = 0) override;
    /// Whether signals wait for work not submitted yet (the GPU command thread submits it when
    /// it runs out of work, or the guest could wait for them forever).
    bool HasUnsubmittedSignals() override;
    /// bbport BB_GUEST_IN_PLACE (draw recording thread): `data` written into the game's memory by the
    /// GPU in stream order, like the command processor's writes; false when the range is not
    /// bound in place (the caller writes it with the CPU).
    bool WriteGuestMemory(VAddr address, const void* data, u32 size);
    /// bbport BB_GUEST_IN_PLACE (in stream order: the recording thread): a WRITE_DATA performed by
    /// the GPU, as the command processor does, instead of a CPU store now (ahead of the GPU work
    /// recorded before it). Values of up to 8 bytes are registered for WAIT_REG_MEM until the GPU
    /// has written them. False: not in place (or BB_GPU_COMMAND_WRITES=0), the caller stores it.
    bool WriteDataOnGpu(VAddr address, const void* data, u32 size) override;
    /// bbport BB_GUEST_IN_PLACE (in stream order: the recording thread): an end-of-pipe label
    /// (EVENT_WRITE_EOP/EOS) written by the GPU itself once the work recorded before it is done,
    /// as the command processor does: a buffer marker into the game's memory, where the CPU sees
    /// it. Registered for WAIT_REG_MEM like the CPU-written ones. False (the caller signals it
    /// from the CPU): not in GPU-visible guest memory, no buffer marker (RecordBufferMarker),
    /// BB_GPU_LABELS=0.
    bool WriteLabelOnGpu(VAddr address, u64 value, u32 num_bytes) override;
    /// bbport: a ZPASS_DONE event (the game's occlusion query) at `address` for `pairs` depth
    /// blocks, in stream order (vk_occlusion.h). False when it is left to the caller (the old
    /// memory model).
    bool OcclusionEvent(VAddr address, u32 pairs) override;
    /// ZPASS_DONE events translated so far.
    u64 OcclusionEvents() const override;
    /// bbport BB_GUEST_IN_PLACE (in stream order): the GPU clock (PS4: 100 MHz) written as 64 bits
    /// at `address` by the GPU, at the end of the pipe or when it gets there (vk_timestamps.h).
    /// False (the caller writes it with the CPU): BB_GPU_TIMESTAMPS=0, no timestamps, the old model.
    bool WriteTimestampOnGpu(VAddr address, bool end_of_pipe) override;
    /// A fault on a page of occlusion counters watched for the game's first access
    /// (BB_OCCLUSION_READ_TRACE); false: not such a page.
    bool OnOcclusionPageAccess(VAddr addr, u64 rip, bool write, bool gpu_thread) override;
    /// A CPU access hit a page whose newest data the GPU wrote into a VRAM copy only
    /// (BB_LAYER_READ_TRAPS): copied back first. False: not such a page.
    bool OnVramDataAccess(VAddr addr, bool assume_locks) override;
    /// End of a guest submission: submits the work recorded so far when signals wait for it and
    /// the last submission is BB_HONEST_FLUSH_US (1000) old: the GPU starts on it as the hardware
    /// would, instead of at the end of the frame, and the guest's mid-frame waits end sooner.
    void SubmitForSignals() override;
    /// bbport BB_PIPE_SIGNALS (default on with honest labels; 0: off): on the GPU command thread with
    /// the draw pipe, fences, the GPU idle and frame signals and the submissions for them are
    /// handed to the draw recording thread as ordered tasks instead of that thread being drained
    /// first (the GPU command thread waited for it ~15 times a frame and the GPU ran dry meanwhile).
    bool PipeSignals() const override;
    /// Stage A: the fences the recording thread ran up to `position` are in the signal queue (or
    /// written): a fence it ran but holds still (PublishSignals) is not visible there yet.
    bool SignalsPublished(u64 position) const override;
    /// Stage A (BB_PIPE_SIGNALS): the recording thread hands over its signals and submits the work
    /// they wait for, in order, when it gets there (one request in flight at a time).
    void RequestSignalFlush() override;
    u64 CurrentTick() const override {
        return backend.CurrentTick();
    }
    /// The newest queued value of the fence at `address` (a WaitRegMem in the same stream is met
    /// by it in stream order).
    bool PendingSignalValue(VAddr address, u64& value) override;
    /// The GPU command thread or the draw recording thread (fault handling runs inline there).
    bool IsGpuSideThread() const override;
    bool IsGpuSideThreadId(u32 tid) const override;
    /// A guest write hit a protected page.
    /// `guest_rip`: the guest instruction that wrote (0: unknown), see BufferCache::NoteCpuWrite.
    bool OnWriteFault(VAddr addr, bool assume_locks, u64 guest_rip = 0) override;
    bool ReadMemory(VAddr addr, u64 size, bool assume_locks = false) override;
    void ProcessDownloadImages() override;
    bool IsMapped(VAddr addr, u64 size) override;
    void MapMemory(VAddr addr, u64 size) override;
    void RegisterMemory(VAddr addr, u64 size) override;
    void UnmapMemory(VAddr addr, u64 size) override;

    void Finish() override;
    void OnSubmit() override;

    template <typename Func>
    void ForEachMappedRangeInRange(VAddr addr, u64 size, Func&& func) {
        const auto range = decltype(mapped_ranges)::interval_type::right_open(addr, addr + size);
        Common::RecursiveSharedLock lock{mapped_ranges_mutex};
        for (const auto& mapped_range : (mapped_ranges & range)) {
            func(mapped_range);
        }
    }

    std::thread::id GetGpuCommandProcessorThread();
#ifdef __linux__
    u32 GetGpuCommandProcessorThreadId();
#endif

protected:
    void PrepareRenderState(const VideoCore::PipelineBase* pipeline);
    RenderState BeginRendering(const VideoCore::PipelineBase* pipeline);
    /// One attachment of a pass as BeginRenderingFull decided it: the target's view in the texture
    /// cache (by id: the slot vector moves its views when it grows), the layout its backing is in
    /// for the pass (the feedback loop layout included) and how the pass loads it.
    struct TargetAttachment {
        VideoCore::ImageViewId view{}; ///< null: no attachment
        vk::ImageLayout layout{};
        /// A scene proxy's or the upscaler's view standing in for `view` (null: none).
        VideoCore::NativeView replacement_view{};
        vk::ImageLayout replacement_layout{};
        std::array<u32, 4> clear_value{};
        bool is_clear = false; ///< colour: the attachment is cleared by the pass
        bool has_depth = false; ///< depth/stencil: the aspects the buffer has and clears
        bool depth_clear = false;
        bool has_stencil = false;
        bool stencil_clear = false;
    };
    /// The pass BeginRenderingFull decided on: attachments and extent (RenderState's
    /// field types: the memo and the scheduler compare it bytewise).
    struct RenderTargets {
        std::array<TargetAttachment, AmdGpu::NUM_COLOR_BUFFERS> color_attachments;
        TargetAttachment depth_stencil_attachment;
        u16 width;
        u16 height;
        u16 num_layers;
        u16 num_color_attachments;
    };
    /// The scene/upscaler decisions, transitions and clear values of the pass of `pipeline`
    /// (RenderTargets), the backend's render state for it (BuildRenderState) and the object motion
    /// attachment.
    RenderState BeginRenderingFull(const VideoCore::PipelineBase* pipeline);
    /// Recording hooks of the backend: the neutral code above calls them 1:1.
    /// The backend's render state (pass description) for `targets`; records nothing.
    virtual RenderState BuildRenderState(const RenderTargets& targets) = 0;
    /// bbport: a draw continuing the open render pass with the same inputs gets the same render
    /// state (RenderStateMemo); nothing can have broken the pass in between (barriers, copies and
    /// dispatches end it).
    struct BeginSignature {
        std::array<VideoCore::ImageId, AmdGpu::NUM_COLOR_BUFFERS + 1> ids{};
        std::array<VideoCore::ImageViewInfo, AmdGpu::NUM_COLOR_BUFFERS + 1> views{};
        u32 mrt_mask = 0;
        u32 num_samples = 0;
        std::array<u8, AmdGpu::NUM_COLOR_BUFFERS> color_samples{};
        bool motion = false;
        bool scene_started = false;
        bool raster_scaling = false;
        u64 upscaler_state = 0;
        u64 generation = 0;
        u32 depth_control = 0;
        bool depth_valid = false;
        bool stencil_valid = false;
        bool operator==(const BeginSignature&) const = default;
    };
    BeginSignature MakeBeginSignature(const VideoCore::PipelineBase* pipeline) const;
    struct BeginMemo {
        bool valid = false;
        BeginSignature signature;
        RenderState state;
        u32 scene_size = 0;
        std::array<float, 2> target_scale{};
    } begin_memo;
    u64 begin_memo_hits = 0, begin_memo_misses = 0;
    void Resolve();
    void DepthStencilCopy(bool is_depth, bool is_stencil);
    void EliminateFastClear();

    bool FilterDraw();
    bool FilterDrawPasses() const;
    /// Everything of a direct draw after the pipeline selection (GPU thread or stage B).
    void DrawRecord(const VideoCore::PipelineBase* pipeline,
                    const Vulkan::PreparedDraw* used_prepared, bool is_indexed, u32 index_offset);

    /// The depth, stencil and depth bias state of the registers, as the dynamic state a backend
    /// sets (the fields behind a disabled `*_enabled` flag are not set).
    struct DepthStencilState {
        bool depth_test_enabled;
        bool depth_write_enabled;
        vk::CompareOp depth_compare_op;
        bool depth_bounds_test_enabled;
        float depth_bounds_min, depth_bounds_max;
        bool depth_bias_enabled;
        float depth_bias_constant, depth_bias_clamp, depth_bias_slope;
        bool stencil_test_enabled;
        StencilOps front_ops, back_ops;
        u32 front_reference, back_reference;
        u32 front_write_mask, back_write_mask;
        u32 front_compare_mask, back_compare_mask;
    };
    void UpdateDepthStencilState(DepthStencilState& state) const;
    /// Primitive restart, rasterizer discard, culling and winding of the registers.
    struct PrimitiveState {
        bool restart_enabled;
        bool rasterizer_discard;
        vk::CullModeFlags cull_mode;
        vk::FrontFace front_face;
    };
    void UpdatePrimitiveState(PrimitiveState& state, bool is_indexed) const;
    /// The viewports and scissors of the registers (Vulkan vocabulary types), in the scaling of
    /// this draw's target.
    struct ViewportScissors {
        boost::container::static_vector<vk::Viewport, AmdGpu::NUM_VIEWPORTS> viewports;
        boost::container::static_vector<vk::Rect2D, AmdGpu::NUM_VIEWPORTS> scissors;
    };
    void UpdateViewportScissorState(ViewportScissors& state) const;
    /// The dynamic state a draw sets from the registers, handed to the recording hooks (one
    /// virtual call for all of it instead of a setter hook each).
    struct DynamicDrawState {
        ViewportScissors viewport_scissors;
        DepthStencilState depth_stencil;
        PrimitiveState primitive;
    };
    void UpdateDynamicState(DynamicDrawState& state, bool is_indexed) const;

    static bool DrawPipeWanted();
    bool UseDrawPipe() const;
    bool OnStageA() const;
    /// Arguments of an indirect draw: `args` holds `max_count` commands `stride` apart.
    struct IndirectDraw {
        VAddr args;
        VAddr count; ///< the draw count, or 0 for max_count draws
        u32 stride;
        u32 max_count;
    };
    /// Everything of an indirect draw after the pipeline selection (GPU thread or stage B).
    void DrawIndirectRecord(const VideoCore::PipelineBase* pipeline, bool is_indexed,
                            const IndirectDraw& indirect);
    /// Everything of an indirect dispatch after the pipeline selection.
    void DispatchIndirectRecord(const VideoCore::PipelineBase* pipeline, VAddr args, u32 size);
    /// Everything of a direct dispatch after the pipeline selection (GPU thread or stage B).
    void DispatchRecord(const VideoCore::PipelineBase* pipeline);
    /// The argument buffers of an indirect draw (the count buffer is null without a count).
    struct IndirectBuffers {
        const VideoCore::Buffer* args;
        u64 args_offset;
        const VideoCore::Buffer* counts;
        u64 counts_offset;
    };
    /// Recording tail of DrawIndirectRecord: the pipeline's descriptors, the dynamic state, the
    /// pass label (MarkPass), the render pass of `state` and the indirect draw itself.
    virtual void RecordDrawIndirect(const VideoCore::PipelineBase* pipeline,
                                    const RenderState& state, bool is_indexed,
                                    const IndirectDraw& indirect, const IndirectBuffers& buffers,
                                    const DynamicDrawState& dynamic) = 0;
    /// What a direct draw issues, from the registers and the fetch shader's offsets.
    struct DrawCall {
        u32 num_indices, num_instances;
        u32 first_vertex, first_instance;
        bool is_indexed;
    };
    /// Recording tail of DrawRecord, after the vertex/index binds and the barrier flush: the
    /// pipeline's descriptors (the push data is final), the dynamic state, the pass label
    /// (MarkPass), the render pass of `state` and the draw itself (pipeline bind, breadcrumb).
    virtual void RecordDraw(const VideoCore::PipelineBase* pipeline,
                            const RenderState& state, const DrawCall& draw,
                            const DynamicDrawState& dynamic) = 0;
    /// Recording tail of DispatchRecord, after the pass has ended: the dispatch's profiler mark,
    /// the pipeline's descriptors and the dispatch of cs_program's group counts.
    virtual void RecordDispatch(const VideoCore::PipelineBase* pipeline, const Shader::Info& cs,
                                const AmdGpu::ComputeProgram& cs_program) = 0;
    /// Recording tail of DispatchIndirectRecord, after the pass has ended: the group counts at
    /// `args_offset` of `args` (the game's `address`, `size` bytes), the descriptors, the dispatch.
    virtual void RecordDispatchIndirect(const VideoCore::PipelineBase* pipeline, VAddr address,
                                        u32 size, const VideoCore::Buffer* args,
                                        u64 args_offset) = 0;
    /// Selects and binds the resources of the pipeline's stages (buffers here, samplers in
    /// BindSamplers, textures in BindTextures): false when a substitution (HLE)
    /// replaced the shader and nothing is left to record.
    bool BindResources(const VideoCore::PipelineBase* pipeline, bool direct_dispatch = false);
    /// A stage's buffer binding as the backend writes it: a null `buffer` is a null descriptor.
    struct BufferBinding {
        const VideoCore::Buffer* buffer;
        u64 offset;
        u64 size;
        bool constant_ring; ///< the bytes are in constant_ring at `offset` (`buffer` is null)
    };
    /// Starts the descriptors of a BindResources: `num_descriptors` (buffers, images and samplers
    /// of all stages) write slots for the binds below, which take theirs from `write_index`.
    virtual void BeginResourceBindings(u32 num_descriptors) = 0;
    /// Binds `buffers` (one per buffer resource of the stage, in order) at `binding`, taking
    /// `write_index` for the descriptor writes of the Vulkan backend.
    virtual void BindBufferSet(std::span<const BufferBinding> buffers,
                               Shader::Backend::Bindings& binding, u32& write_index) = 0;
    /// The buffers of one stage: the selection (constant ring, special buffers, guest memory
    /// through the buffer cache), the push data offsets and the barriers; BindBufferSet writes them.
    void BindBuffers(const Shader::Info& stage, const Vulkan::PreparedStage* prepared,
                     Shader::Backend::Bindings& binding, Shader::PushData& push_data,
                     u32& write_index, bool direct_dispatch = false);

    /// Decides whether the dispatch can be recorded before the held render pass (it touches
    /// nothing the pass's draws touched) and, if so, records it (RecordHoistedDispatch); false:
    /// the caller records it in place.
    bool HoistDispatch(const VideoCore::PipelineBase* pipeline, const Shader::Info& cs,
                       const AmdGpu::ComputeProgram& cs_program);
    /// Whether work that reads only what the current draw binds (its pending accesses and bound
    /// images) may run before the held render pass instead of inside it: the pass's earlier draws
    /// wrote none of it, the draw writes nothing they read, and its images are not the pass's
    /// (or only sampled by both). The Metal backend asks this for a tessellated draw's kernels.
    [[nodiscard]] bool CanHoistDrawWork();
    /// A hoisted dispatch: a memory barrier, the pipeline's descriptors, the dispatch of
    /// cs_program's group counts and a memory barrier, between BeginHoist and EndHoist.
    virtual void RecordHoistedDispatch(const VideoCore::PipelineBase* pipeline,
                                       const Shader::Info& cs,
                                       const AmdGpu::ComputeProgram& cs_program) = 0;
    /// A shader the backend replaces by commands of its own (HLE, the game's buffer copy shader
    /// when it is not translated): true when it was executed and the dispatch is done.
    virtual bool ExecuteShaderHLE(const Shader::Info& cs, const AmdGpu::ComputeProgram& cs_program) {
        return false;
    }
    /// Hands a draw (or, with `cs`, a dispatch) to the recording thread.
    void PostDraw(const VideoCore::PipelineBase* pipeline, const Vulkan::PreparedDraw* used_prepared,
                  bool is_indexed, u32 index_offset, const AmdGpu::ComputeProgram* cs = nullptr,
                  const IndirectDraw* indirect = nullptr);
    /// The compute registers of the dispatch being recorded.
    const AmdGpu::ComputeProgram& CsRegs() const;
    static void RunDrawPacket(void* rasterizer, const u8* packet, u32 size);
    struct RingBinding;
    void CollectRingBindings(const Shader::Info& stage, const Vulkan::PreparedDraw* prepared,
                             boost::container::static_vector<RingBinding, Shader::NUM_BUFFERS>& out);
    void PrintPipeStats();
    /// The registers of the draw being recorded: stage B's copy there, else Liverpool's.
    const AmdGpu::Regs& Regs() const;
    AmdGpu::CbDbExtent CbExtent(u32 index) const;
    AmdGpu::CbDbExtent DbExtent() const;

    /// A stage's image descriptor as the backend writes it: one per binding, a resource that
    /// emulates mip stores with a descriptor array has one per element.
    struct TextureBinding {
        VideoCore::ImageViewId view{}; ///< null: a null descriptor (unless `replacement_view`)
        /// A scene proxy's or the upscaler's view standing in for `view` (null: none).
        VideoCore::NativeView replacement_view{};
        vk::ImageLayout layout{}; ///< the layout the descriptor's image is read in
        u32 array_index = 0;      ///< position in its resource's descriptor array
        bool storage = false;     ///< a storage image, otherwise sampled
    };
    using TextureBindings = boost::container::static_vector<TextureBinding, Shader::NUM_IMAGES>;
    /// Binds `textures` (the stage's image resources in order; the elements of a resource follow
    /// each other, its first has array_index 0) at `binding`, taking `write_index` for the
    /// descriptor writes of the Vulkan backend. Also called on the bind helper thread.
    virtual void BindTextureSet(std::span<const TextureBinding> textures,
                                Shader::Backend::Bindings& binding, u32& write_index) = 0;
    /// The images of one stage: the lookups in the texture cache, proxy eligibility, the
    /// transitions for the use, then BindTextureSet and BindSamplers.
    /// `on_helper`: run by the texture binding helper; out-of-date images are not refreshed.
    void BindTextures(const Shader::Info& stage, const Vulkan::PreparedStage* prepared,
                      Shader::Backend::Bindings& binding, u32& write_index, bool& barrier,
                      bool on_helper = false);
    /// bbport: a stage's resolved textures remembered by its prepared T# hashes
    /// (TextureSetMemo): a hit only redoes the per-draw effects (binding flags, transitions).
    struct TextureSetEntry {
        VideoCore::ImageId id{}; ///< after the depth redirect; null descriptor when invalid
        VideoCore::ImageViewId view{}; ///< of a native entry
        VideoCore::NativeView proxy_view{}; ///< of a proxy entry (SampleProxy's view)
        const void* backing = nullptr;
        VideoCore::SubresourceRange range;
        bool proxy = false; ///< sampled from the scene proxy (view: SampleProxy on each use)
    };
    struct TextureSet {
        u64 key = 0;
        const Shader::Info* stage = nullptr;
        u64 generation = ~0ull;
        u64 scene_generation = 0; ///< SceneTargets::Generation of the proxy views
        u32 count = 0;
        static constexpr u32 MaxImages = 16; ///< larger sets are not memoized
        std::array<u64, MaxImages> hashes{};
        std::array<TextureSetEntry, MaxImages> entries{};
    };
    std::array<TextureSet, 32768> texture_sets{};
    /// Returns true when the stage's images were bound from the memo (into `textures`).
    bool BindTexturesFromSet(const Shader::Info& stage, const Vulkan::PreparedStage* prepared,
                             bool& barrier, TextureSet*& slot, TextureBindings& textures);
    /// The id of `view`, which FindTexture/FindView just returned for `image`.
    VideoCore::ImageViewId ViewIdOf(const VideoCore::Image& image, const VideoCore::ImageView& view);

    void ForceAnisotropy(AmdGpu::Sampler& sharp, bool is_depth) const;
    SamplerMemo<SamplerRef> sampler_memo;
    SamplerRef CachedSampler(const AmdGpu::Sampler& sharp, bool is_depth, float lod_bias);

    /// bbport: the samplers of `stage` (S# or the prepared sharps, with this draw's LOD bias) from
    /// the texture cache, handed to the backend in one call.
    void BindSamplers(const Shader::Info& stage, const Vulkan::PreparedStage* prepared,
                      Shader::Backend::Bindings& binding, u32& write_index);
    /// Binds `samplers` (one per sampler resource of the stage, in order) at `binding`, taking
    /// `write_index` for the descriptor writes of the Vulkan backend.
    virtual void BindSamplerSet(std::span<const VideoCore::SamplerRef> samplers,
                                Shader::Backend::Bindings& binding, u32& write_index) = 0;

    /// bbport: the vertex and index buffers of the draw being recorded, as host buffers and
    /// ranges (the vertex input descriptions are Vulkan vocabulary): resolved here from the
    /// registers and the pipeline, recorded by the backend (EmitDrawBuffers).
    struct VertexBinds {
        VideoCore::VertexInputs<vk::VertexInputAttributeDescription2EXT> attributes;
        VideoCore::VertexInputs<vk::VertexInputBindingDescription2EXT> bindings;
        VideoCore::VertexInputs<const VideoCore::Buffer*> host_buffers; ///< null: no buffer
        VideoCore::VertexInputs<u64> host_offsets;
        VideoCore::VertexInputs<u64> host_sizes;
        VideoCore::VertexInputs<u64> host_strides;
        u32 num_buffers = 0;
    } vertex_binds;
    struct IndexBind {
        const VideoCore::Buffer* buffer;
        u64 offset;
        vk::IndexType type;
    } index_bind{};
    /// The direct draw whose vertex/index buffers BindResources may resolve early.
    struct DrawInputs {
        const VideoCore::PipelineBase* pipeline;
        const Vulkan::PreparedDraw* prepared;
        u32 index_offset;
        bool is_indexed;
        bool pending;
        bool resolved;
    } draw_inputs{};
    void ResolveVertexBuffers(const VideoCore::PipelineBase* pipeline,
                              const Vulkan::PreparedDraw* prepared);
    void ResolveIndexBuffer(u32 index_offset);
    /// Records the binds of vertex_binds (`vertex`) and of index_bind (`index`).
    virtual void EmitDrawBuffers(bool vertex, bool index) = 0;

    /// End of a draw or dispatch: the held pass's access sets, the images' bindings and the
    /// buffers' reads and writes for the next barrier.
    void ResetBindings(bool is_compute);

    /// BB_GPU_PROFILE: names the render pass `state` is about to begin where it starts. Only
    /// the backend with a profiler enables it (pass_labels).
    struct PassLabel {
        u64 key; ///< hash of the pass's size, shaders and formats
        u32 width, height;
        u32 num_color_attachments;
        u32 color_present; ///< bit per colour attachment backed by an image
        std::array<vk::Format, AmdGpu::NUM_COLOR_BUFFERS> color_formats;
        bool has_depth;
        vk::Format depth_format;
        u64 vs_hash, ps_hash;
    };
    void MarkPass(const VideoCore::PipelineBase* pipeline, const RenderState& state);
    virtual void SetPassLabel(const PassLabel& label) = 0;
    bool pass_labels = false;
    /// BB_GPU_PROFILE: the display pass starts a frame: the profiler closes the last one.
    virtual void ProfilerBeginFrame() {}

    /// Texture set memo statistics (BB_FRAME_STATS).
    u64 texture_set_hits = 0, texture_set_misses = 0;
    std::array<u64, 4> texture_set_why{}; ///< misses: other key, generation, image check, new
    /// The prepared sharps of `stage` in the current prepared draw, if they fit it.
    const Vulkan::PreparedStage* FindPreparedStage(const Shader::Info& stage) const;
    /// Whether the helper may bind textures for this pipeline at all (fixed descriptor layout,
    /// no storage writes that invalidate cached images).
    bool HelperEligible(const VideoCore::PipelineBase* pipeline) const;
    /// Whether every texture of `stage` takes the memoized path without refreshing, creating or
    /// redirecting anything that records commands or touches the buffer cache.
    bool TexturesBindableOnHelper(const Shader::Info& stage, const Vulkan::PreparedStage* prepared);
    static void RunTextureTask(void* rasterizer);
    static void JoinBindHelper(void* rasterizer);

    /// Display pass: counts the frame (BbStats::gpu_frames) and, with BB_BUFFER_STATS, the lag.
    void NoteFrameStart();
    /// BB_GPU_PROFILE: a timestamp where a render pass starts (vk_gpu_profiler.h).
    [[nodiscard]] bool DrawsNothing(const VideoCore::PipelineBase* pipeline,
                                    const RenderState& state) const;

    bool IsComputeMetaClear(const VideoCore::PipelineBase* pipeline);
    bool IsComputeImageCopy(const VideoCore::PipelineBase* pipeline, bool dry_run = false);
    bool IsComputeImageClear(const VideoCore::PipelineBase* pipeline);

    /// magic_enum::enum_contains, from a table: it scans every enumerator (per texture per draw).
    static bool IsKnownFormat(AmdGpu::DataFormat data_fmt, AmdGpu::NumberFormat num_fmt) {
        static const auto tables = [] {
            std::pair<std::array<bool, 256>, std::array<bool, 256>> t{};
            for (const auto value : magic_enum::enum_values<AmdGpu::DataFormat>()) {
                if (static_cast<u32>(value) < 256) {
                    t.first[static_cast<u32>(value)] = true;
                }
            }
            for (const auto value : magic_enum::enum_values<AmdGpu::NumberFormat>()) {
                if (static_cast<u32>(value) < 256) {
                    t.second[static_cast<u32>(value)] = true;
                }
            }
            return t;
        }();
        const u32 d = static_cast<u32>(data_fmt), n = static_cast<u32>(num_fmt);
        return d < 256 && n < 256 && tables.first[d] && tables.second[n];
    }

    /// Stops the threads that call into the backend (the signal thread, the draw recording
    /// thread) and drops what they hold, in the order the members used to be destroyed: a
    /// backend calls it first in its destructor, before its own members go.
    void ShutdownThreads();

    friend class VideoCore::BufferCache;

    VideoCore::CacheBackend& backend;
    VideoCore::GpuQueue& queue;
    VideoCore::PageManager page_manager;
    VideoCore::BufferCache buffer_cache;
    VideoCore::TextureCache texture_cache;
    AmdGpu::Liverpool* liverpool;
    Core::MemoryManager* memory;
    boost::icl::interval_set<VAddr> mapped_ranges;
    Common::SharedFirstMutex mapped_ranges_mutex;
    std::unique_ptr<Vulkan::DrawPreparation> draw_prep;
    /// The scene services (bbport: motion vectors and FSR, docs/upscaler.md): owned by the backend's
    /// rasterizer, never null (a backend without them has the Null* objects in them).
    VideoCore::CameraMotionApi* camera_motion;
    VideoCore::ScenePasses* scene_targets;
    bool scene_started = false;
    VideoCore::ObjectMotionApi* object_motion;
    bool motion_draw = false;
    u64 motion_geometry{};    ///< vertex-stream identity of the current direct draw
    bool gbuffer_draw = false;
    VideoCore::UpscalerApi* upscaler;
    bool upscaler_enabled = false; ///< upscaler->Enabled() and camera_motion->Enabled(), as
    bool camera_enabled = false;   ///< of SetSceneServices
    /// Sets the scene services the core uses (null: none, as with upscaling and object motion
    /// disabled). A backend calls it in its constructor, before it binds the rasterizer.
    void SetSceneServices(VideoCore::ScenePasses* scene, VideoCore::UpscalerApi* upscaler_,
                          VideoCore::CameraMotionApi* camera, VideoCore::ObjectMotionApi* object);
    std::array<float, 2> draw_jitter{};         ///< viewport offset of the current draw, pixels
    std::array<float, 2> target_scale{1.0f, 1.0f}; ///< pass drawn into the upscaler's output-size images
    const bool host_markers_enabled;
    const bool guest_markers_enabled;

    using RenderTargetInfo = std::pair<VideoCore::ImageId, VideoCore::TextureCache::ImageDesc>;
    std::array<RenderTargetInfo, AmdGpu::NUM_COLOR_BUFFERS> cb_descs;
    std::pair<VideoCore::ImageId, VideoCore::TextureCache::ImageDesc> db_desc;
    boost::container::static_vector<VideoCore::ImageId, Shader::NUM_IMAGES> bound_images;
    struct BoundBuffer {
        const VideoCore::Buffer* buffer;
        u64 offset;
        u32 size;
        bool is_written;
        u32 logical_stage=7;
        u64 shader=0;
        VAddr guest_address=0;
    };
    boost::container::static_vector<BoundBuffer, Shader::NUM_BUFFERS> bound_buffers;

    /// bbport BB_HOIST_DISPATCH (GpuQueue::HoldingPass): the game's memory the current
    /// draw or dispatch binds in place, collected while binding; a draw's goes into the held render
    /// pass's sets, a dispatch is checked against them.
    struct GuestAccess {
        VAddr address;
        u64 size;
        bool write;
    };
    std::vector<GuestAccess> pending_accesses;
    bool pending_untracked = false; ///< a binding whose range is not known (paged, GDS)
    boost::icl::interval_set<VAddr> pass_reads, pass_writes;
    /// Images the held pass's draws bound (attachments too): a hoisted dispatch may not touch them.
    boost::container::small_vector<u32, 32> pass_images;
    /// Of those, the ones only sampled (read-only layouts) so far.
    boost::container::small_vector<u32, 32> pass_read_images;
    /// O(1) membership for the two vectors above: an image index is a member when its stamp equals
    /// pass_generation (bumped whenever the vectors are cleared; stamps start at 0, generation at 1).
    std::vector<u64> pass_image_stamp, pass_read_stamp;
    u64 pass_generation = 1;
    bool InPassImages(u32 index) const {
        return index < pass_image_stamp.size() && pass_image_stamp[index] == pass_generation;
    }
    bool InPassReadImages(u32 index) const {
        return index < pass_read_stamp.size() && pass_read_stamp[index] == pass_generation;
    }
    static void SetStamp(std::vector<u64>& stamps, u32 index, u64 value) {
        if (index >= stamps.size()) {
            stamps.resize(std::max<size_t>(index + 1, stamps.size() * 2), 0);
        }
        stamps[index] = value;
    }
    u64 pass_serial = 0;
    bool pass_untracked = false;
    void NoteGuestAccess(VAddr address, u64 size, bool write) {
        if (queue.HoistEnabled() && size != 0) {
            pending_accesses.push_back({address, size, write});
        }
    }
    /// The copy shader's ranges, from its control list in the game's memory.
    bool NoteCopyShaderAccesses(const Shader::Info& cs, const AmdGpu::ComputeProgram& cs_program);

    Shader::PushData push_data;

    // bbport: bindings point at their description instead of copying it (a hot spot): into
    // image_desc_cache for memoized lookups (pinned for the current BindTextures call), else
    // into image_desc_storage.
    using ImageBindingInfo = std::pair<VideoCore::ImageId, const VideoCore::TextureCache::ImageDesc*>;
    // bbport: texture descriptions depend only on the T# and three resource flags; building
    // them (mip layout sizes) for every binding of every draw was a hot spot.
    struct ImageDescCacheEntry {
        std::array<u64, 4> sharp{};
        u32 flags = ~0u;
        VideoCore::TextureCache::ImageDesc desc;
        // Memoized FindImage for bindings without mip overrides (TextureBindingMemo).
        u64 found_generation = ~0ULL;
        VideoCore::ImageId found_id{};
        VideoCore::TextureCache::ImageDesc found_desc;
        u64 pinned = 0; ///< bind_epoch of the BindTextures call referencing found_desc
        u64 last_use = 0;
        // Memoized FindView for found_id (after the depth redirect): valid while the entry's
        // FindImage memo holds and the image keeps this backing (TextureViewMemo).
        VideoCore::TextureCache::ViewMemo view_memo;
    };
    std::array<ImageDescCacheEntry, 4096> image_desc_cache{};
    u64 desc_use_counter = 0;
    /// Entries replacing pinned cache slots during one BindTextures call.
    boost::container::static_vector<ImageDescCacheEntry, Shader::NUM_IMAGES> image_desc_overflow;
    boost::container::static_vector<VideoCore::TextureCache::ImageDesc, Shader::NUM_IMAGES * 2>
        image_desc_storage;
    u64 bind_epoch = 0;
    // bbport: render/depth target lookups memoized by their raw register bytes while image
    // registrations are unchanged (the descriptions depend only on those registers).
    struct TargetMemo {
        std::array<u8, 256> key{};
        u32 key_size = 0;
        u64 generation = ~0ULL;
        VideoCore::ImageId image_id{};
        VideoCore::TextureCache::ImageDesc desc;
    };
    std::array<TargetMemo, 64> target_memo{};
    // bbport: consecutive draws mostly keep their targets; the slot's description (cb_descs,
    // db_desc) is then still the right one and is neither looked up nor copied.
    struct LastTarget {
        std::array<u8, 256> key{};
        u32 key_size = 0;
        u64 generation = ~0ULL;
        VideoCore::ImageId image_id{};
    };
    std::array<LastTarget, AmdGpu::NUM_COLOR_BUFFERS + 1> last_targets{}; ///< CBs, then DB
    template <typename... Parts>
    VideoCore::ImageId FindTargetMemoized(VideoCore::TextureCache::ImageDesc& desc,
                                          LastTarget& last, auto&& make_desc,
                                          const Parts&... parts);
    /// The prepared draw whose pipeline the current Draw uses (its sharps are valid), or null.
    const Vulkan::PreparedDraw* bind_prepared = nullptr;
    /// `hash` is ImageDescHash(sharp, res), computed by a draw-preparation worker or here.
    ImageDescCacheEntry& CachedImageDescEntry(const AmdGpu::Image& sharp,
                                              const Shader::ImageResource& res, u64 hash);
    ImageDescCacheEntry& CachedImageDescEntry(const AmdGpu::Image& sharp,
                                              const Shader::ImageResource& res) {
        return CachedImageDescEntry(sharp, res, Vulkan::ImageDescHash(sharp, res));
    }
    const VideoCore::TextureCache::ImageDesc& CachedImageDesc(const AmdGpu::Image& sharp,
                                                              const Shader::ImageResource& res) {
        return CachedImageDescEntry(sharp, res).desc;
    }
    boost::container::static_vector<ImageBindingInfo, Shader::NUM_IMAGES> image_bindings;
    // bbport: textures of a draw bound on the helper thread while this thread binds buffers.
    static bool BindHelperWanted();
    VideoCore::BindHelper bind_helper{BindHelperWanted()};
    struct TextureTask {
        struct Stage {
            const Shader::Info* info;
            const Vulkan::PreparedStage* prepared;
            Shader::Backend::Bindings binding;
            u32 write_index;
        };
        std::array<Stage, Shader::MaxStageTypes> stages;
        u32 count = 0;
        u32 completed = 0; ///< stages the helper bound; the rest are bound after the join
        bool barrier = false;
    } texture_task;
    /// Draws whose textures the helper bound completely, partly or not at all (BB_FRAME_STATS).
    u64 helper_full = 0, helper_partial = 0, helper_serial = 0;
    /// The memoized cache entry of each image binding (FindImage memo taken), else null.
    boost::container::static_vector<ImageDescCacheEntry*, Shader::NUM_IMAGES> image_binding_entries;
    bool fault_process_pending{};
    bool attachment_feedback_loop{};
    bool needs_barrier{};
    // bbport: two-stage draw pipeline (stage B state; its thread stops first: ShutdownThreads).
    static inline thread_local const AmdGpu::Regs* stage_regs = nullptr;
    static inline thread_local const AmdGpu::ComputeProgram* stage_cs = nullptr;
    AmdGpu::Regs pipe_regs{};
    bool pipe_synced = false;
    std::array<AmdGpu::CbDbExtent, AmdGpu::NUM_COLOR_BUFFERS> pipe_cb_extent{};
    AmdGpu::CbDbExtent pipe_db_extent{};
    /// Stage A: small read-only guest buffers copied into the constant ring for a packet.
    struct RingBinding {
        u32 index;  ///< buffer resource of the stage
        u32 size;
        u64 offset; ///< in the ring
        VAddr address;
    };
    /// Stage A: the newest draw pipe packet that reads guest memory, hashed per granule: 256 bytes
    /// for bindings up to 4 KiB, 16 KiB up to 256 KiB, 1 MiB above (a note costs a few granules,
    /// a lookup checks all three). A collision only makes a write wait longer.
    struct PendingReads {
        static constexpr u64 Shifts[3] = {8, 14, 20};
        static constexpr u64 Limits[2] = {4_KB, 256_KB};
        static constexpr u32 Bits = 16;
        std::array<std::vector<u64>, 3> tables{std::vector<u64>(1u << Bits),
                                               std::vector<u64>(1u << Bits),
                                               std::vector<u64>(1u << Bits)};
        static u64 Slot(u64 granule, u32 level) {
            return ((granule * 4 + level) * 0x9E3779B97F4A7C15ull) >> (64 - Bits);
        }
        void Note(VAddr address, u64 size, u64 packet) {
            const u32 level = size <= Limits[0] ? 0 : size <= Limits[1] ? 1 : 2;
            auto& table = tables[level];
            for (u64 g = address >> Shifts[level]; g <= (address + size - 1) >> Shifts[level];
                 ++g) {
                u64& slot = table[Slot(g, level)];
                slot = std::max(slot, packet);
            }
        }
        u64 Newest(VAddr address, u64 size) const {
            u64 newest = 0;
            for (u32 level = 0; level < 3; ++level) {
                for (u64 g = address >> Shifts[level]; g <= (address + size - 1) >> Shifts[level];
                     ++g) {
                    newest = std::max(newest, tables[level][Slot(g, level)]);
                }
            }
            return newest;
        }
    };
    PendingReads pending_reads;
    PendingReads pending_write_table; ///< the same per granule for GPU writes queued (NotePendingGpuWrite)
    u64 untracked_reads_packet = 0; ///< newest packet reading guest memory it does not list
    u64 constram_waits_skipped = 0; ///< statistics: CPU writes that did not have to wait
    u64 proxy_samples = 0; ///< texture bindings that read a scene proxy (statistics)
    float sampler_lod_bias = 0.0f; ///< bbport: extra bias of this draw's samplers
    bool pipeline_is_compute = false; ///< bbport: the bound pipeline of BindResources
    bool scene_debug_frame = false; ///< BB_SCENE_DEBUG: this frame's passes are printed
    u32 preupload_frame = ~0u; ///< bbport: frame of the last background pre-upload
    bool PendingWriteOverlaps(VAddr address, u64 size);
    /// Stage B: the ring bindings of the stages of the packet being recorded.
    struct RingStage {
        const Shader::Info* info;
        const RingBinding* bindings;
        u32 count;
    };
    std::array<RingStage, Shader::MaxStageTypes> ring_stages{};
    u32 num_ring_stages = 0;
    /// bbport: the vertex stream V#s of the packet being recorded (stage B; empty elsewhere).
    std::span<const AmdGpu::Buffer> packet_vsharps;
    const RingBinding* FindRingBinding(const Shader::Info& stage, u32 index) const {
        for (u32 i = 0; i < num_ring_stages; ++i) {
            if (ring_stages[i].info == &stage) {
                for (u32 j = 0; j < ring_stages[i].count; ++j) {
                    if (ring_stages[i].bindings[j].index == index) {
                        return &ring_stages[i].bindings[j];
                    }
                }
                return nullptr;
            }
        }
        return nullptr;
    }
    std::unique_ptr<VideoCore::ConstantRing> constant_ring;
    std::unique_ptr<VideoCore::OcclusionApi> occlusion;
    std::unique_ptr<VideoCore::TimestampApi> timestamps;
    /// The backend's occlusion queries and GPU clock, made at first use; null: not supported (the
    /// caller writes with the CPU).
    virtual std::unique_ptr<VideoCore::OcclusionApi> CreateOcclusion() {
        return nullptr;
    }
    virtual std::unique_ptr<VideoCore::TimestampApi> CreateTimestamps() {
        return nullptr;
    }
    /// Submissions (prepared draws) kept alive until stage B reaches the position.
    std::deque<std::pair<u64, std::shared_ptr<const void>>> pipe_keepalive;
    std::unique_ptr<Vulkan::DrawPipe> draw_pipe;

    /// BB_HONEST_LABELS (SignalAfterGpu). Its thread stops before the scheduler goes
    /// (ShutdownThreads).
    struct GpuSignal {
        u64 tick;
        std::function<void()> signal;
        VAddr label;
        u64 value;
    };
    /// The signal thread body: waits for the GPU on the signals' ticks (WaitGpuTick) and runs them.
    void GpuSignalLoop(std::stop_token token);
    /// Waits up to `timeout_ns` for the GPU to finish the submission of `tick` (the work
    /// semaphore); false on a timeout. A lost device is the backend's to report.
    virtual bool WaitGpuTick(u64 tick, u64 timeout_ns) = 0;
    /// Records an end-of-pipe write of `value` (4 or 8 bytes; the high half first) at `offset` of
    /// `buffer`, done by the GPU once the work recorded before it is (WriteLabelOnGpu).
    virtual void RecordBufferMarker(const VideoCore::Buffer* buffer, u64 offset, u64 value,
                                    u32 num_bytes) = 0;
    /// Whether RecordBufferMarker is cheap enough for every label (false: the labels are written
    /// by the signal thread once the GPU is done, BB_GPU_LABELS=0's path).
    virtual bool BufferMarkersSupported() const {
        return true;
    }
    /// bbport: GpuSignalLoop has waited 10 s for `tick`: prints whether the GPU or the port is stuck.
    void GpuWatchdog(u64 tick);
    std::mutex gpu_signals_mutex;
    std::condition_variable_any gpu_signals_cv;
    std::deque<GpuSignal> gpu_signals;
    /// bbport: signals registered on the draw recording thread since it last handed them over
    /// (PublishSignals: at the end of each guest submission and at every flush), one lock per
    /// batch instead of per fence (~450 fences a frame).
    std::vector<GpuSignal> local_signals;
    std::atomic<u32> local_signal_count{0};
    void PublishSignals();
    /// Draw pipe position up to which the recording thread's signals are handed over: fences
    /// handed to it before are in gpu_signals (or written).
    std::atomic<u64> signals_published_upto{0};
    std::atomic<bool> signal_flush_requested{false};
    std::jthread gpu_signal_thread;
    void SubmitForSignalsNow();
    /// steady_clock nanoseconds of the last submission (written by both pipeline stages).
    std::atomic<s64> last_flush_ns{0};
};

inline const AmdGpu::Regs& RasterizerCore::Regs() const {
    return stage_regs ? *stage_regs : liverpool->regs;
}

inline AmdGpu::CbDbExtent RasterizerCore::CbExtent(u32 index) const {
    return stage_regs ? pipe_cb_extent[index] : liverpool->last_cb_extent[index];
}

inline const AmdGpu::ComputeProgram& RasterizerCore::CsRegs() const {
    return stage_cs ? *stage_cs : liverpool->GetCsRegs();
}

inline AmdGpu::CbDbExtent RasterizerCore::DbExtent() const {
    return stage_regs ? pipe_db_extent : liverpool->last_db_extent;
}

} // namespace VideoCore
