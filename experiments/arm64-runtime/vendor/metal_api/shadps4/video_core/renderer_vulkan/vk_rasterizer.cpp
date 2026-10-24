// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <chrono>
#include <dlfcn.h>
#include <map>
#include <unordered_set>
#include <xxhash.h>
#include "video_core/renderer_vulkan/ui_composition.h"
#include "bbport_timeline.h"
#include "bbport_sections.h"
#include "game_profile.h"
#include "bbport_ce_stats.h"
#include "bbport_toggles.h"
#include "bbport_write_log.h"
#include "bbport_free_check.h"
#include "bbport_guest_memory.h"
#include "video_core/renderer_vulkan/vk_frame_capture.h"
#include "common/hash.h"
#include "common/debug.h"
#include "core/debug_state.h"
#include "core/emulator_settings.h"
#include "core/memory.h"
#include "shader_recompiler/runtime_info.h"
#include "video_core/amdgpu/liverpool.h"
#include "video_core/amdgpu/pm4_cmds.h"
#include "video_core/buffer_cache/buffer.h"
#include "video_core/buffer_cache/buffer_cache.h"
#include "video_core/renderer_vulkan/liverpool_to_vk.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"
#include "video_core/renderer_vulkan/vk_rasterizer.h"
#include "video_core/renderer_vulkan/vk_gpu_profiler.h"
#include "bbport_threads.h"
#include "video_core/renderer_vulkan/vk_runtime.h"
#include "video_core/renderer_vulkan/vk_scene_services.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_hle.h"
#include "video_core/renderer_vulkan/vk_gpu_labels.h"
#include "video_core/renderer_vulkan/vk_indirect_guard.h"
#include "video_core/renderer_vulkan/vk_occlusion.h"
#include "video_core/renderer_vulkan/vk_timestamps.h"
#include "video_core/texture_cache/image_view.h"
#include "video_core/texture_cache/texture_cache.h"

namespace Vulkan {

Rasterizer::Rasterizer(const Instance& instance_, Scheduler& scheduler_, Runtime& runtime_,
                       AmdGpu::Liverpool* liverpool_)
    : RasterizerCore{runtime_, scheduler_, liverpool_}, instance{instance_}, scheduler{scheduler_},
      runtime{runtime_},
      pipeline_cache{instance, scheduler, liverpool, buffer_cache.GetSparsePageShift()} {
    // Before the rasterizer is bound: Liverpool enqueues buffers only once it sees it.
    draw_prep = std::make_unique<DrawPreparation>(pipeline_cache);
    scene_services = std::make_unique<SceneServices>(instance, scheduler, runtime, texture_cache);
    SetSceneServices(scene_services->targets_adapter.get(), scene_services->upscaler_adapter.get(),
                     scene_services->camera_adapter.get(), scene_services->object_adapter.get());
    if (!EmulatorSettings.IsNullGPU()) {
        liverpool->BindRasterizer(this);
    }
    BbGuestMemory::Install(instance); // bbport: before the runtime maps direct memory
    memory->SetRasterizer(this);

    GpuProfiler::Init(instance, scheduler);
    pass_labels = GpuProfiler::Get() != nullptr;
    if (DrawPipeWanted()) {
        constant_ring = std::make_unique<VideoCore::ConstantRing>(runtime);
        draw_pipe = std::make_unique<DrawPipe>(&RunDrawPacket,
                                               static_cast<VideoCore::RasterizerCore*>(this));
    }
    // bbport: this thread joins the texture binding helper before it changes image state.
    runtime.SetImageAccessHook(&JoinBindHelper, static_cast<VideoCore::RasterizerCore*>(this));
    // The scheduler calls this with its own SubmitInfo; the sparse arena adds its semaphores to it.
    scheduler.SetSubmitCallback([this](VideoCore::SubmitInfo& info) {
        runtime.FlushBarriers();
        buffer_cache.SubmitPendingArenaBinds(static_cast<Vulkan::SubmitInfo&>(info));
    });
}

TemporalUpscaler& Rasterizer::GetUpscaler() noexcept {
    return *scene_services->upscaler;
}

Rasterizer::~Rasterizer() {
    // The recording thread and the signal thread call into this class and its pipeline cache: they
    // stop, and the draw preparation workers (which hold the pipeline cache) go, before the
    // members of this class are destroyed.
    ShutdownThreads();
    draw_prep.reset();
}

void Rasterizer::RecordBufferMarker(const VideoCore::Buffer* buffer, u64 offset, u64 value,
                                    u32 num_bytes) {
    static const bool force_portable = [] {
        const char* env = std::getenv("BB_GPU_LABELS_PORTABLE");
        return env && env[0] != '0';
    }();
    const bool marker = instance.IsBufferMarkerSupported() && !force_portable;
    static const bool announced = [&] {
        std::printf("GPU: end-of-pipe labels are written by the GPU (%s; "
                    "BB_GPU_LABELS=0: by a CPU thread after the GPU)\n",
                    marker ? "AMD buffer markers" : "portable Vulkan updates");
        return true;
    }();
    (void)announced;
    if (!marker) {
        // Core Vulkan transfer commands are forbidden inside a render pass. Preserve command
        // order by ending it here, then restart rendering normally at the next draw.
        scheduler.EndRendering();
        runtime.FlushBarriers();
    }
    // The 64-bit label as two 32-bit end-of-pipe writes, the high half first: whoever sees the
    // new low half (the guest polls that) sees the new high half too.
    scheduler.Record([buffer = buffer->Handle(), offset, value,
                      num_bytes, marker](vk::CommandBuffer cmdbuf) {
        if (!marker) {
            RecordPortableGpuLabel(cmdbuf, buffer, offset, value, num_bytes);
            return;
        }
        if (num_bytes == 8) {
            cmdbuf.writeBufferMarkerAMD(vk::PipelineStageFlagBits::eBottomOfPipe, buffer,
                                        offset + 4, static_cast<u32>(value >> 32));
        }
        cmdbuf.writeBufferMarkerAMD(vk::PipelineStageFlagBits::eBottomOfPipe, buffer, offset,
                                    static_cast<u32>(value));
    });
    if (!marker) {
        runtime.AccessBuffer(buffer, offset, num_bytes,
                             vk::PipelineStageFlagBits2::eTransfer,
                             vk::AccessFlagBits2::eTransferWrite);
    }
}

std::unique_ptr<VideoCore::OcclusionApi> Rasterizer::CreateOcclusion() {
    return std::make_unique<OcclusionQueries>(instance, scheduler, buffer_cache, runtime);
}

std::unique_ptr<VideoCore::TimestampApi> Rasterizer::CreateTimestamps() {
    return std::make_unique<GpuTimestamps>(instance, scheduler, buffer_cache, runtime);
}

bool Rasterizer::ExecuteShaderHLE(const Shader::Info& cs, const AmdGpu::ComputeProgram& cs_program) {
    return Vulkan::ExecuteShaderHLE(cs, Regs(), cs_program, *this, runtime);
}

void Rasterizer::ProfilerBeginFrame() {
    if (auto* profiler = GpuProfiler::Get()) {
        profiler->BeginFrame();
    }
}

bool Rasterizer::WaitGpuTick(u64 tick, u64 timeout_ns) {
    const vk::Semaphore handle = scheduler.GetWorkSemaphore()->Handle();
    const vk::SemaphoreWaitInfo wait_info = {
        .semaphoreCount = 1,
        .pSemaphores = &handle,
        .pValues = &tick,
    };
    const vk::Result result = instance.GetDevice().waitSemaphores(&wait_info, timeout_ns);
    if (result == vk::Result::eSuccess) {
        return true;
    }
    if (result == vk::Result::eErrorDeviceLost) {
        Breadcrumbs::ReportDeviceLost("waiting for the GPU");
        ASSERT_MSG(false, "Device lost while waiting for the GPU");
    }
    return false;
}

void Rasterizer::RecordDraw(const VideoCore::PipelineBase* pipeline_base, const RenderState& state,
                            const DrawCall& draw, const DynamicDrawState& dynamic) {
    const auto* pipeline = static_cast<const GraphicsPipeline*>(pipeline_base);
    {
        BB_SECTION(PipelineBind);
        pipeline->BindResources(set_writes, push_data, {image_infos.data(), image_infos.size()},
                                {buffer_infos.data(), buffer_infos.size()});
    }
    ApplyDynamicState(pipeline, dynamic);
    MarkPass(pipeline, state);
    const auto& vs_info = pipeline->GetStage(Shader::SwStage::Vertex);
    const auto* ps_info = pipeline->GetStages()[u32(Shader::SwStage::Fragment)];
    scheduler.pass_label_shaders = {vs_info.pgm_hash, ps_info ? ps_info->pgm_hash : 0};
    scheduler.BeginRendering(state);

    const vk::Pipeline handle = pipeline->Handle();
    const bool is_indexed = draw.is_indexed;
    const u32 num_indices = draw.num_indices, num_instances = draw.num_instances;
    const u32 first_vertex = draw.first_vertex, first_instance = draw.first_instance;
    const Breadcrumbs::Crumb crumb{
        .kind = is_indexed ? Breadcrumbs::Kind::DrawIndexed : Breadcrumbs::Kind::Draw,
        .hash = {vs_info.pgm_hash, ps_info ? ps_info->pgm_hash : 0},
        .program = {vs_info.ProgramBase(), ps_info ? ps_info->ProgramBase() : 0},
        .count = {num_indices, num_instances, 0},
    };
    scheduler.RecordCrumb(crumb, [=](vk::CommandBuffer cmdbuf) {
        cmdbuf.bindPipeline(vk::PipelineBindPoint::eGraphics, handle);
        if (is_indexed) {
            cmdbuf.drawIndexed(num_indices, num_instances, 0, s32(first_vertex), first_instance);
        } else {
            cmdbuf.draw(num_indices, num_instances, first_vertex, first_instance);
        }
    });
}

void Rasterizer::RecordDrawIndirect(const VideoCore::PipelineBase* pipeline_base, const RenderState& state,
                                    bool is_indexed, const IndirectDraw& indirect,
                                    const IndirectBuffers& buffers,
                                    const DynamicDrawState& dynamic) {
    const auto* pipeline = static_cast<const GraphicsPipeline*>(pipeline_base);
    const VAddr count_address = indirect.count;
    const u32 stride = indirect.stride;
    const u32 max_count = indirect.max_count;

    pipeline->BindResources(set_writes, push_data, {image_infos.data(), image_infos.size()},
                            {buffer_infos.data(), buffer_infos.size()});
    ApplyDynamicState(pipeline, dynamic);
    MarkPass(pipeline, state);
    scheduler.BeginRendering(state);

    ASSERT(stride == (is_indexed ? sizeof(VkDrawIndexedIndirectCommand)
                                 : sizeof(VkDrawIndirectCommand)));
    const vk::Pipeline handle = pipeline->Handle();
    const vk::Buffer args = buffers.args->Handle();
    const u64 args_offset = buffers.args_offset;
    const vk::Buffer counts = count_address != 0 ? buffers.counts->Handle() : vk::Buffer{};
    const u64 counts_offset = count_address != 0 ? buffers.counts_offset : 0;
    const auto* vs_crumb = pipeline->GetStages()[u32(Shader::SwStage::Vertex)];
    const auto* ps_crumb = pipeline->GetStages()[u32(Shader::SwStage::Fragment)];
    const Breadcrumbs::Crumb crumb{
        .kind = Breadcrumbs::Kind::DrawIndirect,
        .hash = {vs_crumb ? vs_crumb->pgm_hash : 0, ps_crumb ? ps_crumb->pgm_hash : 0},
        .program = {vs_crumb ? vs_crumb->ProgramBase() : 0,
                    ps_crumb ? ps_crumb->ProgramBase() : 0},
        .address = indirect.args,
        .count = {max_count, stride, 0},
    };
    scheduler.RecordCrumb(crumb, [=](vk::CommandBuffer cmdbuf) {
        cmdbuf.bindPipeline(vk::PipelineBindPoint::eGraphics, handle);
        if (is_indexed) {
            if (counts) {
                cmdbuf.drawIndexedIndirectCount(args, args_offset, counts, counts_offset, max_count,
                                                stride);
            } else {
                cmdbuf.drawIndexedIndirect(args, args_offset, max_count, stride);
            }
        } else if (counts) {
            cmdbuf.drawIndirectCount(args, args_offset, counts, counts_offset, max_count, stride);
        } else {
            cmdbuf.drawIndirect(args, args_offset, max_count, stride);
        }
    });
}

void Rasterizer::RecordDispatch(const VideoCore::PipelineBase* pipeline_base, const Shader::Info& cs,
                                const AmdGpu::ComputeProgram& cs_program) {
    const auto* pipeline = static_cast<const ComputePipeline*>(pipeline_base);
    // After the resource binding: its transfers (image uploads, downloads) mark themselves.
    if (auto* profiler = GpuProfiler::Get()) {
        profiler->Mark(cs.pgm_hash ^ 0xD15Aull, [&] {
            return fmt::format("dispatch cs {:016x} ({}x{}x{} groups)", cs.pgm_hash,
                               cs_program.dim_x, cs_program.dim_y, cs_program.dim_z);
        });
    }
    pipeline->BindResources(set_writes, push_data, {image_infos.data(), image_infos.size()},
                            {buffer_infos.data(), buffer_infos.size()});

    const vk::Pipeline handle = pipeline->Handle();
    const u32 dim_x = cs_program.dim_x, dim_y = cs_program.dim_y, dim_z = cs_program.dim_z;
    if (FrameCapture::Active()) {
        FrameCapture::Dispatch(cs.pgm_hash, dim_x, dim_y, dim_z);
    }
    const Breadcrumbs::Crumb crumb{
        .kind = Breadcrumbs::Kind::Dispatch,
        .hash = {cs.pgm_hash, 0},
        .program = {cs.ProgramBase(), 0},
        .count = {dim_x, dim_y, dim_z},
    };
    scheduler.RecordCrumb(crumb, [=](vk::CommandBuffer cmdbuf) {
        cmdbuf.bindPipeline(vk::PipelineBindPoint::eCompute, handle);
        cmdbuf.dispatch(dim_x, dim_y, dim_z);
    });
}

void Rasterizer::RecordDispatchIndirect(const VideoCore::PipelineBase* pipeline_base, VAddr address, u32 size,
                                        const VideoCore::Buffer* buffer, u64 base) {
    const auto* pipeline = static_cast<const ComputePipeline*>(pipeline_base);
    // Group counts checked on the GPU first (vk_indirect_guard.h); recorded before the
    // pipeline's bindings, which the check's own pipeline would disturb.
    vk::Buffer args = buffer->Handle();
    u64 args_offset = base;
    if (IndirectGuard::Enabled()) {
        if (!indirect_guard) {
            indirect_guard = std::make_unique<IndirectGuard>(instance, scheduler, runtime);
        }
        std::tie(args, args_offset) = indirect_guard->CheckArgs(*buffer, base);
    }
    pipeline->BindResources(set_writes, push_data, {image_infos.data(), image_infos.size()},
                            {buffer_infos.data(), buffer_infos.size()});

    const vk::Pipeline handle = pipeline->Handle();
    const vk::Buffer raw_args = buffer->Handle();
    const u64 raw_offset = base;
    const auto& cs_crumb = pipeline->GetStage(Shader::SwStage::Compute);
    const u32 args_slot = Breadcrumbs::ArgsSlot();
    const Breadcrumbs::Crumb crumb{
        .kind = Breadcrumbs::Kind::DispatchIndirect,
        .hash = {cs_crumb.pgm_hash, 0},
        .program = {cs_crumb.ProgramBase(), 0},
        .address = address,
        .count = {buffer_cache.IsInPlace(address, size) ? 1u : 0u, 0, 0},
        .args_slot = args_slot,
    };
    scheduler.RecordCrumb(crumb, [=](vk::CommandBuffer cmdbuf) {
        Breadcrumbs::CopyArgs(cmdbuf, raw_args, raw_offset, args_slot); // what the game wrote
        cmdbuf.bindPipeline(vk::PipelineBindPoint::eCompute, handle);
        cmdbuf.dispatchIndirect(args, args_offset);
    });
}

void Rasterizer::BindSamplerSet(std::span<const VideoCore::SamplerRef> samplers,
                                Shader::Backend::Bindings& binding, u32& write_index) {
    for (const VideoCore::SamplerRef& sampler : samplers) {
        image_infos.emplace_back(sampler.handle, VK_NULL_HANDLE, vk::ImageLayout::eGeneral);
        auto& set_write = set_writes[write_index++];
        set_write.dstSet = VK_NULL_HANDLE;
        set_write.dstBinding = binding.unified++;
        set_write.dstArrayElement = 0;
        set_write.descriptorCount = 1;
        set_write.descriptorType = vk::DescriptorType::eSampler;
        set_write.pImageInfo = &image_infos.back();
    }
}

void Rasterizer::BeginResourceBindings(u32 num_descriptors) {
    set_writes.clear();
    set_writes.resize(num_descriptors);
    buffer_infos.clear();
    image_infos.clear();
}

void Rasterizer::BindBufferSet(std::span<const BufferBinding> buffers,
                               Shader::Backend::Bindings& binding, u32& write_index) {
    for (const auto& bound : buffers) {
        if (bound.constant_ring) {
            buffer_infos.emplace_back(constant_ring->Handle(), bound.offset, bound.size);
        } else if (!bound.buffer) {
            buffer_infos.emplace_back(VK_NULL_HANDLE, 0, VK_WHOLE_SIZE);
        } else {
            buffer_infos.emplace_back(bound.buffer->Handle(), bound.offset, bound.size);
        }
        auto& set_write = set_writes[write_index++];
        set_write.dstSet = VK_NULL_HANDLE;
        set_write.dstBinding = binding.unified++;
        set_write.dstArrayElement = 0;
        set_write.descriptorCount = 1;
        set_write.descriptorType = vk::DescriptorType::eStorageBuffer;
        set_write.pBufferInfo = &buffer_infos.back();
    }
}

void Rasterizer::EmitDrawBuffers(bool vertex, bool index) {
    if (vertex) {
        EmitVertexBuffers();
    }
    if (index) {
        EmitIndexBuffer();
    }
}

void Rasterizer::RecordHoistedDispatch(const VideoCore::PipelineBase* pipeline_base, const Shader::Info& cs,
                                       const AmdGpu::ComputeProgram& cs_program) {
    const auto* pipeline = static_cast<const ComputePipeline*>(pipeline_base);
    // Before the pass, between barriers: after what precedes the pass, before the pass and
    // whatever follows it. No accesses tracked for it: the barrier after it covers them.
    static constexpr vk::MemoryBarrier2 before{
        .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
        .srcAccessMask = vk::AccessFlagBits2::eMemoryWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite,
    };
    static constexpr vk::MemoryBarrier2 after{
        .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .srcAccessMask = vk::AccessFlagBits2::eShaderWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
        .dstAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
    };
    scheduler.BeginHoist();
    scheduler.Record([](vk::CommandBuffer cmdbuf) {
        cmdbuf.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &before});
    });
    runtime.FlushImageBarriers();
    pipeline->BindResources(set_writes, push_data, {image_infos.data(), image_infos.size()},
                            {buffer_infos.data(), buffer_infos.size()});
    const vk::Pipeline handle = pipeline->Handle();
    const u32 dim_x = cs_program.dim_x, dim_y = cs_program.dim_y, dim_z = cs_program.dim_z;
    const Breadcrumbs::Crumb crumb{
        .kind = Breadcrumbs::Kind::Dispatch,
        .hash = {cs.pgm_hash, 0},
        .program = {cs.ProgramBase(), 0},
        .count = {dim_x, dim_y, dim_z},
    };
    scheduler.RecordCrumb(crumb, [=](vk::CommandBuffer cmdbuf) {
        cmdbuf.bindPipeline(vk::PipelineBindPoint::eCompute, handle);
        cmdbuf.dispatch(dim_x, dim_y, dim_z);
    });
    scheduler.Record([](vk::CommandBuffer cmdbuf) {
        cmdbuf.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &after});
    });
    scheduler.EndHoist();
}

void Rasterizer::ApplyDynamicState(const GraphicsPipeline* pipeline,
                                   const DynamicDrawState& dynamic) const {
    BB_SECTION(UpdateDynamicState);
    SetViewportsScissors(dynamic.viewport_scissors);
    SetDepthStencilState(dynamic.depth_stencil);
    SetPrimitiveState(dynamic.primitive);
    UpdateRasterizationState();
    UpdateColorBlendingState(pipeline);

    auto& dynamic_state = scheduler.GetDynamicState();
    dynamic_state.CommitWith(instance.IsDepthBoundsSupported(),
                             instance.IsDynamicColorWriteMaskSupported(),
                             instance.IsAttachmentFeedbackLoopLayoutSupported(),
                             [&](auto&& command) { scheduler.Record(std::move(command)); });
}

void Rasterizer::SetViewportsScissors(const ViewportScissors& state) const {
    auto& dynamic_state = scheduler.GetDynamicState();
    dynamic_state.SetViewports(state.viewports);
    dynamic_state.SetScissors(state.scissors);
}

void Rasterizer::SetDepthStencilState(const DepthStencilState& state) const {
    auto& dynamic_state = scheduler.GetDynamicState();

    dynamic_state.SetDepthTestEnabled(state.depth_test_enabled);
    if (state.depth_test_enabled) {
        dynamic_state.SetDepthWriteEnabled(state.depth_write_enabled);
        dynamic_state.SetDepthCompareOp(state.depth_compare_op);
    }

    dynamic_state.SetDepthBoundsTestEnabled(state.depth_bounds_test_enabled);
    if (state.depth_bounds_test_enabled) {
        dynamic_state.SetDepthBounds(state.depth_bounds_min, state.depth_bounds_max);
    }

    dynamic_state.SetDepthBiasEnabled(state.depth_bias_enabled);
    if (state.depth_bias_enabled) {
        dynamic_state.SetDepthBias(state.depth_bias_constant, state.depth_bias_clamp,
                                   state.depth_bias_slope);
    }

    dynamic_state.SetStencilTestEnabled(state.stencil_test_enabled);
    if (state.stencil_test_enabled) {
        dynamic_state.SetStencilOps(state.front_ops, state.back_ops);
        dynamic_state.SetStencilReferences(state.front_reference, state.back_reference);
        dynamic_state.SetStencilWriteMasks(state.front_write_mask, state.back_write_mask);
        dynamic_state.SetStencilCompareMasks(state.front_compare_mask, state.back_compare_mask);
    }
}

void Rasterizer::SetPrimitiveState(const PrimitiveState& state) const {
    auto& dynamic_state = scheduler.GetDynamicState();
    dynamic_state.SetPrimitiveRestartEnabled(state.restart_enabled);
    dynamic_state.SetRasterizerDiscardEnabled(state.rasterizer_discard);
    dynamic_state.SetCullMode(state.cull_mode);
    dynamic_state.SetFrontFace(state.front_face);
}

void Rasterizer::SetPassLabel(const PassLabel& label) {
    auto* profiler = GpuProfiler::Get();
    if (!profiler) {
        return;
    }
    profiler->Mark(label.key, [&] {
        std::string targets;
        for (u32 cb = 0; cb < label.num_color_attachments; ++cb) {
            targets += (label.color_present >> cb & 1) ? vk::to_string(label.color_formats[cb])
                                                       : std::string{"-"};
            targets += ' ';
        }
        return fmt::format("pass {}x{} [{}] depth {} vs {:016x} ps {:016x}", label.width,
                           label.height, targets,
                           label.has_depth ? vk::to_string(label.depth_format) : std::string{"-"},
                           label.vs_hash, label.ps_hash);
    });
}

void Rasterizer::Draw(bool is_indexed, u32 index_offset, const PreparedDraw* prepared) {
    RENDERER_TRACE;
    BbStats::draws.fetch_add(1, std::memory_order_relaxed);

    // bbport: with the draw pipeline this thread only selects the pipeline and hands the draw
    // to the recording thread (DrawRecord there); draws FilterDraw handles itself run here.
    const bool pipelined = UseDrawPipe();
    if (pipelined && !FilterDrawPasses() &&
        !BbToggle::Disabled(BbToggle::PipelinedMemoryWrites)) {
        PostDraw(nullptr, nullptr, false, 0);
        return;
    }
    if (!pipelined || !FilterDrawPasses()) {
        DrainDrawPipe(DrawPipe::ReasonDraw);
        FrameCapture::Poll();
        scheduler.PopPendingOperations();
        if (!FilterDraw()) {
            return;
        }
    }
    const u64 explained_before = BbStats::ExplainedNullPipelines();
    const GraphicsPipeline* pipeline = pipeline_cache.GetGraphicsPipeline({}, prepared);
    const PreparedDraw* used_prepared = pipeline_cache.UsedPrepared();
    if (prepared) {
        draw_prep->Count(pipeline != nullptr &&
                         prepared->state.load(std::memory_order_acquire) == PreparedDraw::Ready &&
                         prepared->reg_checksum == liverpool->gfx_reg_checksum);
    }
    if (!pipeline) {
        if (BbStats::ExplainedNullPipelines() == explained_before) {
            BbStats::NoteDrop(BbStats::DropKind::NoPipeline);
        }
        return;
    }
    if (pipelined && FilterDrawPasses()) {
        PostDraw(pipeline, used_prepared, is_indexed, index_offset);
        return;
    }
    DrawRecord(pipeline, used_prepared, is_indexed, index_offset);
}

void Rasterizer::DrawIndirect(bool is_indexed, VAddr arg_address, u32 offset, u32 stride,
                              u32 max_count, VAddr count_address, u16 vertex_sgpr_offset,
                              u16 instance_sgpr_offset) {
    RENDERER_TRACE;
    // bbport: like direct draws, handed to the recording thread after the pipeline selection
    // (else the GPU thread waited here for a whole frame of queued draws).
    const bool pipelined = UseDrawPipe() && FilterDrawPasses() &&
                           !BbToggle::Disabled(BbToggle::PipelinedIndirectDraws);
    if (!pipelined) {
        DrainDrawPipe();
        scheduler.PopPendingOperations();
        if (!FilterDraw()) {
            return;
        }
    }

    const DrawIndirectParams params = {
        .vertex_sgpr_offset = vertex_sgpr_offset,
        .instance_sgpr_offset = instance_sgpr_offset,
    };
    const u64 explained_before = BbStats::ExplainedNullPipelines();
    const GraphicsPipeline* pipeline = pipeline_cache.GetGraphicsPipeline(params, nullptr, true);
    if (!pipeline) {
        if (BbStats::ExplainedNullPipelines() == explained_before) {
            BbStats::NoteDrop(BbStats::DropKind::NoPipeline);
        }
        return;
    }
    const IndirectDraw indirect = {arg_address + offset, count_address, stride, max_count};
    if (pipelined) {
        PostDraw(pipeline, nullptr, is_indexed, 0, nullptr, &indirect);
        return;
    }
    DrawIndirectRecord(pipeline, is_indexed, indirect);
}

void Rasterizer::DispatchDirect() {
    RENDERER_TRACE;
    BbStats::dispatches.fetch_add(1, std::memory_order_relaxed);
    // bbport: like draws, handed to the draw recording thread after the pipeline selection.
    const bool pipelined = UseDrawPipe() && !BbToggle::Disabled(BbToggle::PipelinedDispatch);
    if (!pipelined) {
        DrainDrawPipe();
    }
    const ComputePipeline* pipeline = pipeline_cache.GetComputePipeline();
    if (!pipeline) {
        BbStats::NoteDrop(BbStats::DropKind::NoPipeline);
        return;
    }
    if (pipelined) {
        PostDraw(pipeline, nullptr, false, 0, &CsRegs());
        return;
    }
    DispatchRecord(pipeline);
}

void Rasterizer::DispatchIndirect(VAddr address, u32 offset, u32 size) {
    RENDERER_TRACE;
    // bbport: handed to the recording thread like direct dispatches.
    const bool pipelined = UseDrawPipe() && !BbToggle::Disabled(BbToggle::PipelinedIndirectDraws);
    if (!pipelined) {
        DrainDrawPipe();
    }
    const ComputePipeline* pipeline = pipeline_cache.GetComputePipeline();
    if (!pipeline) {
        BbStats::NoteDrop(BbStats::DropKind::NoPipeline);
        return;
    }
    if (pipelined) {
        const IndirectDraw indirect = {address + offset, 0, size, 1};
        PostDraw(pipeline, nullptr, false, 0, &CsRegs(), &indirect);
        return;
    }
    DispatchIndirectRecord(pipeline, address + offset, size);
}

u64 Rasterizer::Flush() {
    BB_SECTION(Flush);
    DrainDrawPipe();
    PublishSignals(); // the recording thread's (it is idle here, or this is it)
    last_flush_ns.store(std::chrono::steady_clock::now().time_since_epoch().count(),
                        std::memory_order_relaxed);
    const u64 current_tick = scheduler.CurrentTick();
    SubmitInfo info{};
    scheduler.Flush(info);
    BbTimeline::Note(BbTimeline::VulkanSubmit, current_tick, DrawPipe::OnStageB() ? 1 : 0);
    return current_tick;
}

void Rasterizer::EmitVertexBuffers() {
    BB_SECTION(EmitVertexBuffers);
    // bbport: only the used entries go into the recording chunk (the static vectors hold 32 of
    // each: ~3 KiB copied per draw).
    auto& v = vertex_binds;
    const bool dynamic_input = instance.IsVertexInputDynamicState();
    const u32 num_buffers = v.num_buffers;
    scheduler.ReserveRecordData(
        v.bindings.size() * sizeof(v.bindings[0]) + v.attributes.size() * sizeof(v.attributes[0]) +
        num_buffers * (sizeof(vk::Buffer) + 3 * sizeof(vk::DeviceSize)) + 128);
    if (dynamic_input) {
        scheduler.Record([bindings = scheduler.RecordData(std::span<const vk::VertexInputBindingDescription2EXT>{v.bindings.data(), v.bindings.size()}),
                          attributes = scheduler.RecordData(std::span<const vk::VertexInputAttributeDescription2EXT>{
                              v.attributes.data(), v.attributes.size()})](
                             vk::CommandBuffer cmdbuf) {
            cmdbuf.setVertexInputEXT(bindings, attributes);
        });
    }
    if (num_buffers == 0) {
        return;
    }
    std::array<vk::Buffer, MaxVertexBufferCount> handles;
    for (u32 i = 0; i < num_buffers; ++i) {
        handles[i] = v.host_buffers[i] ? v.host_buffers[i]->Handle() : vk::Buffer{};
    }
    const auto buffers = scheduler.RecordData(std::span<const vk::Buffer>{handles.data(), num_buffers});
    const auto offsets = scheduler.RecordData(std::span<const vk::DeviceSize>{v.host_offsets.data(), num_buffers});
    if (dynamic_input) {
        scheduler.Record([num_buffers, buffers, offsets](vk::CommandBuffer cmdbuf) {
            cmdbuf.bindVertexBuffers(0, num_buffers, buffers.data(), offsets.data());
        });
        return;
    }
    const auto sizes = scheduler.RecordData(std::span<const vk::DeviceSize>{v.host_sizes.data(), num_buffers});
    const auto strides = scheduler.RecordData(std::span<const vk::DeviceSize>{v.host_strides.data(), num_buffers});
    scheduler.Record([num_buffers, buffers, offsets, sizes, strides](vk::CommandBuffer cmdbuf) {
        cmdbuf.bindVertexBuffers2(0, num_buffers, buffers.data(), offsets.data(), sizes.data(),
                                  strides.data());
    });
}

void Rasterizer::EmitIndexBuffer() {
    scheduler.Record([handle = index_bind.buffer->Handle(), offset = index_bind.offset,
                      type = index_bind.type](vk::CommandBuffer cmdbuf) {
        cmdbuf.bindIndexBuffer(handle, offset, type);
    });
}

void Rasterizer::BindTextureSet(std::span<const TextureBinding> textures,
                                Shader::Backend::Bindings& binding, u32& write_index) {
    const u32 first_image_idx = image_infos.size();
    for (const auto& texture : textures) {
        vk::ImageView view = ImageViewOf(texture.replacement_view);
        if (!view && texture.view) {
            view = *texture_cache.GetImageView(texture.view).image_view;
        }
        image_infos.emplace_back(VK_NULL_HANDLE, view, texture.layout);
    }
    u32 image_info_idx = first_image_idx;
    for (u32 i = 0; i < textures.size();) {
        // A resource: its first binding and the array elements after it.
        u32 array_size = 1;
        while (i + array_size < textures.size() && textures[i + array_size].array_index != 0) {
            ++array_size;
        }
        auto& set_write = set_writes[write_index++];
        set_write.dstSet = VK_NULL_HANDLE;
        set_write.dstBinding = binding.unified;
        set_write.dstArrayElement = 0;
        set_write.descriptorCount = array_size;
        set_write.descriptorType = textures[i].storage ? vk::DescriptorType::eStorageImage
                                                       : vk::DescriptorType::eSampledImage;
        set_write.pImageInfo = &image_infos[image_info_idx];

        image_info_idx += array_size;
        i += array_size;
        binding.unified += array_size;
    }
}

RenderState Rasterizer::BuildRenderState(const RenderTargets& targets) {
    const auto attachment_of = [&](const TargetAttachment& source) {
        RenderAttachment attachment{};
        if (source.replacement_view) {
            attachment.image_view = source.replacement_view;
            attachment.image_layout = source.replacement_layout;
        } else if (source.view) {
            attachment.image_view =
                NativeViewOf(*texture_cache.GetImageView(source.view).image_view);
            attachment.image_layout = source.layout;
        }
        attachment.clear_value = source.clear_value;
        return attachment;
    };
    RenderState state;
    state.width = targets.width;
    state.height = targets.height;
    state.num_layers = targets.num_layers;
    state.num_color_attachments = targets.num_color_attachments;
    for (u32 cb = 0; cb < state.color_attachments.size(); ++cb) {
        state.color_attachments[cb] = attachment_of(targets.color_attachments[cb]);
        state.color_attachments[cb].is_clear = targets.color_attachments[cb].is_clear;
    }
    const auto& depth = targets.depth_stencil_attachment;
    auto& attachment = state.depth_stencil_attachment;
    attachment = attachment_of(depth);
    attachment.is_clear = 0;
    attachment.has_depth = depth.has_depth;
    attachment.depth_clear = depth.depth_clear;
    attachment.has_stencil = depth.has_stencil;
    attachment.stencil_clear = depth.stencil_clear;
    return state;
}

void Rasterizer::UpdateRasterizationState() const {
    const auto& regs = Regs();
    auto& dynamic_state = scheduler.GetDynamicState();
    dynamic_state.SetLineWidth(regs.line_control.Width());
}

void Rasterizer::UpdateColorBlendingState(const GraphicsPipeline* pipeline) const {
    const auto& regs = Regs();
    auto& dynamic_state = scheduler.GetDynamicState();
    dynamic_state.SetBlendConstants(regs.blend_constants);
    dynamic_state.SetColorWriteMasks(pipeline->GetGraphicsKey().write_masks);
    dynamic_state.SetAttachmentFeedbackLoopEnabled(attachment_feedback_loop);
}

void Rasterizer::ScopeMarkerBegin(const std::string_view& str, bool from_guest) {
    DrainDrawPipe();
    if ((from_guest && !EmulatorSettings.IsVkGuestMarkersEnabled()) ||
        (!from_guest && !EmulatorSettings.IsVkHostMarkersEnabled())) {
        return;
    }
    const auto cmdbuf = scheduler.CommandBuffer();
    cmdbuf.beginDebugUtilsLabelEXT(vk::DebugUtilsLabelEXT{
        .pLabelName = str.data(),
    });
}

void Rasterizer::ScopeMarkerEnd(bool from_guest) {
    DrainDrawPipe();
    if ((from_guest && !EmulatorSettings.IsVkGuestMarkersEnabled()) ||
        (!from_guest && !EmulatorSettings.IsVkHostMarkersEnabled())) {
        return;
    }
    const auto cmdbuf = scheduler.CommandBuffer();
    cmdbuf.endDebugUtilsLabelEXT();
}

void Rasterizer::ScopedMarkerInsert(const std::string_view& str, bool from_guest) {
    DrainDrawPipe();
    if ((from_guest && !EmulatorSettings.IsVkGuestMarkersEnabled()) ||
        (!from_guest && !EmulatorSettings.IsVkHostMarkersEnabled())) {
        return;
    }
    const auto cmdbuf = scheduler.CommandBuffer();
    cmdbuf.insertDebugUtilsLabelEXT(vk::DebugUtilsLabelEXT{
        .pLabelName = str.data(),
    });
}

void Rasterizer::ScopedMarkerInsertColor(const std::string_view& str, const u32 color,
                                         bool from_guest) {
    DrainDrawPipe();
    if ((from_guest && !EmulatorSettings.IsVkGuestMarkersEnabled()) ||
        (!from_guest && !EmulatorSettings.IsVkHostMarkersEnabled())) {
        return;
    }
    const auto cmdbuf = scheduler.CommandBuffer();
    cmdbuf.insertDebugUtilsLabelEXT(vk::DebugUtilsLabelEXT{
        .pLabelName = str.data(),
        .color = std::array<f32, 4>(
            {(f32)((color >> 16) & 0xff) / 255.0f, (f32)((color >> 8) & 0xff) / 255.0f,
             (f32)(color & 0xff) / 255.0f, (f32)((color >> 24) & 0xff) / 255.0f})});
}

} // namespace Vulkan
