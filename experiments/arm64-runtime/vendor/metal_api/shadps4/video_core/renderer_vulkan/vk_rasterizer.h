// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "video_core/rasterizer_core.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"

namespace Vulkan {

class GraphicsPipeline;
class Runtime;
struct SceneServices;
class TemporalUpscaler;

/// The Vulkan backend's rasterizer: the recording half. The neutral half (stage A/B protocol,
/// signals, memory, caches) is VideoCore::RasterizerCore.
class Rasterizer final : public VideoCore::RasterizerCore {
public:
    explicit Rasterizer(const Instance& instance, Scheduler& scheduler, Runtime& runtime,
                        AmdGpu::Liverpool* liverpool);
    ~Rasterizer() override;

    void Draw(bool is_indexed, u32 index_offset = 0,
              const PreparedDraw* prepared = nullptr) override;
    void DrawIndirect(bool is_indexed, VAddr arg_address, u32 offset, u32 size, u32 max_count,
                      VAddr count_address, u16 vertex_sgpr_offset,
                      u16 instance_sgpr_offset) override;

    void DispatchDirect() override;
    void DispatchIndirect(VAddr address, u32 offset, u32 size) override;

    void ScopeMarkerBegin(const std::string_view& str, bool from_guest = false) override;
    void ScopeMarkerEnd(bool from_guest = false) override;
    void ScopedMarkerInsert(const std::string_view& str, bool from_guest = false) override;
    void ScopedMarkerInsertColor(const std::string_view& str, const u32 color,
                                 bool from_guest = false) override;

    u64 Flush() override;

    PipelineCache& GetPipelineCache() {
        return pipeline_cache;
    }

    /// The temporal upscaler (the presenter draws and dumps the output-size frames).
    [[nodiscard]] TemporalUpscaler& GetUpscaler() noexcept;

private:
    // Recording hooks of VideoCore::RasterizerCore.
    RenderState BuildRenderState(const RenderTargets& targets) override;
    void RecordDraw(const VideoCore::PipelineBase* pipeline, const RenderState& state,
                    const DrawCall& draw, const DynamicDrawState& dynamic) override;
    void RecordDrawIndirect(const VideoCore::PipelineBase* pipeline, const RenderState& state,
                            bool is_indexed, const IndirectDraw& indirect,
                            const IndirectBuffers& buffers,
                            const DynamicDrawState& dynamic) override;
    void RecordDispatch(const VideoCore::PipelineBase* pipeline, const Shader::Info& cs,
                        const AmdGpu::ComputeProgram& cs_program) override;
    void RecordDispatchIndirect(const VideoCore::PipelineBase* pipeline, VAddr address, u32 size,
                                const VideoCore::Buffer* args, u64 args_offset) override;
    void BindTextureSet(std::span<const TextureBinding> textures,
                        Shader::Backend::Bindings& binding, u32& write_index) override;
    bool WaitGpuTick(u64 tick, u64 timeout_ns) override;
    void RecordBufferMarker(const VideoCore::Buffer* buffer, u64 offset, u64 value,
                            u32 num_bytes) override;
    void BindSamplerSet(std::span<const VideoCore::SamplerRef> samplers,
                        Shader::Backend::Bindings& binding, u32& write_index) override;
    void SetPassLabel(const PassLabel& label) override;
    void BeginResourceBindings(u32 num_descriptors) override;
    void BindBufferSet(std::span<const BufferBinding> buffers, Shader::Backend::Bindings& binding,
                       u32& write_index) override;
    void EmitDrawBuffers(bool vertex, bool index) override;
    void RecordHoistedDispatch(const VideoCore::PipelineBase* pipeline, const Shader::Info& cs,
                               const AmdGpu::ComputeProgram& cs_program) override;
    std::unique_ptr<VideoCore::OcclusionApi> CreateOcclusion() override;
    std::unique_ptr<VideoCore::TimestampApi> CreateTimestamps() override;
    void ProfilerBeginFrame() override;
    bool ExecuteShaderHLE(const Shader::Info& cs, const AmdGpu::ComputeProgram& cs_program) override;

    /// The dynamic state of a draw into the scheduler's DynamicState, committed as commands.
    void ApplyDynamicState(const GraphicsPipeline* pipeline,
                           const DynamicDrawState& dynamic) const;
    void SetViewportsScissors(const ViewportScissors& state) const;
    void SetDepthStencilState(const DepthStencilState& state) const;
    void SetPrimitiveState(const PrimitiveState& state) const;
    void UpdateRasterizationState() const;
    void UpdateColorBlendingState(const GraphicsPipeline* pipeline) const;

    /// bbport: the binds of the core's vertex_binds and index_bind, as commands.
    void EmitVertexBuffers();
    void EmitIndexBuffer();

    const Instance& instance;
    Scheduler& scheduler;
    Runtime& runtime;
    std::unique_ptr<SceneServices> scene_services; ///< targets, motion vectors, upscaler
    PipelineCache pipeline_cache;
    boost::container::static_vector<vk::DescriptorImageInfo, Shader::NUM_IMAGES> image_infos;
    boost::container::static_vector<vk::DescriptorBufferInfo, Shader::NUM_BUFFERS> buffer_infos;

    Pipeline::DescriptorWrites set_writes;
    std::unique_ptr<class IndirectGuard> indirect_guard; ///< stage B only (lazily)
};

} // namespace Vulkan
