// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <memory>

#include "video_core/rasterizer_core.h"
#include "video_core/renderer_metal/mtl_pipeline_cache.h"

namespace Metal {

using VideoCore::RenderAttachment;
using VideoCore::RenderState;

class CameraMotion;
class Device;
class Runtime;
class Scheduler;
class SceneTargets;
class SceneTargetsAdapter;
class Upscaler;
class VelocityMirror;

/// bbport: the Metal backend's rasterizer (plan P4.3b): the recording hooks of the shared
/// VideoCore::RasterizerCore (stage A/B protocol, signals, memory, caches and resource selection
/// are the core's). Encoding is immediate, on the thread that calls the hooks (the draw recording
/// thread with the draw pipe, else the GPU command thread), into Scheduler's encoders.
///
/// Bindings: the recompiler numbers every resource of a pipeline's stages with one running
/// binding (Shader::Backend::Bindings::unified, SPIR-V set 0); the Bind*Set hooks collect the
/// resources by that number, the draw or dispatch then sets each stage's Metal slots from its
/// CompiledShader's binding map (mtl_shader.h: push constants at buffer 0 via set*Bytes).
class Rasterizer final : public VideoCore::RasterizerCore {
public:
    Rasterizer(Device& device, Scheduler& scheduler, Runtime& runtime,
               AmdGpu::Liverpool* liverpool);
    ~Rasterizer() override;

    void Draw(bool is_indexed, u32 index_offset = 0,
              const Vulkan::PreparedDraw* prepared = nullptr) override;
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
        return *pipeline_cache;
    }

    /// The scene services: the UI composition of a scaled session (the presenter draws and dumps
    /// the output-size frames).
    [[nodiscard]] Upscaler& GetUpscaler() noexcept {
        return *metal_upscaler;
    }
    /// The camera tracking and motion vector pass of the scene services (temporal upscaling).
    [[nodiscard]] CameraMotion& GetCameraMotion() noexcept {
        return *camera_motion;
    }

    struct Impl; ///< Objective-C state of the recording (mtl_rasterizer.mm)

private:
    std::unique_ptr<VideoCore::OcclusionApi> CreateOcclusion() override;
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
    void BeginResourceBindings(u32 num_descriptors) override;
    void BindBufferSet(std::span<const BufferBinding> buffers, Shader::Backend::Bindings& binding,
                       u32& write_index) override;
    void BindTextureSet(std::span<const TextureBinding> textures,
                        Shader::Backend::Bindings& binding, u32& write_index) override;
    void BindSamplerSet(std::span<const VideoCore::SamplerRef> samplers,
                        Shader::Backend::Bindings& binding, u32& write_index) override;
    void EmitDrawBuffers(bool vertex, bool index) override;
    void RecordHoistedDispatch(const VideoCore::PipelineBase* pipeline, const Shader::Info& cs,
                               const AmdGpu::ComputeProgram& cs_program) override;
    void SetPassLabel(const PassLabel& label) override;
    bool WaitGpuTick(u64 tick, u64 timeout_ns) override;
    void RecordBufferMarker(const VideoCore::Buffer* buffer, u64 offset, u64 value,
                            u32 num_bytes) override;
    /// Labels are written by the signal thread (mtl_scheduler_design.md "Labels and fences").
    bool BufferMarkersSupported() const override {
        return false;
    }

    /// Why a draw or dispatch is not encoded (counted per reason, each logged once; P4.3d lifts
    /// PrimitiveEmulated and IndirectCount, the others are Metal limits or empty work).
    enum class Drop : u32 {
        None,
        PrimitiveEmulated, ///< QuadList, fans, adjacency, patches (RectList: see RectListDraw)
        FillPoint,         ///< polygon mode point
        RasterizerDiscard,
        CullAll,           ///< front and back culled on a triangle topology
        EmptyScissor,
        Uint8Index, ///< unused: uint8 indices are widened (IndexExpand)
        IndexOffsetAlignment,
        VertexStrideAlignment,
        IndirectCount, ///< a count buffer
        EmptyDraw,     ///< no instances or no indices
        NoIndexBuffer,
        NoVertexBuffers,
        EmptyDispatch,
        IndirectAlignment,
        NoShader,
        RectListDraw,        ///< an indexed or indirect RectList draw
        RectListVertexStart, ///< a RectList draw whose first vertex is not a multiple of 3
        IndexDataUnreadable, ///< expansion needs the index data, its buffer has no CPU view
        IndirectEmulated,    ///< an indirect draw of a topology the backend expands
        TessellationInstanced, ///< a tessellated draw of more than one instance
        TessellationIndirect,  ///< an indirect tessellated draw
        GeometryInstanced,     ///< a geometry draw of more than one instance
        GeometryTooLarge,      ///< a geometry draw whose scratch memory would exceed the limit
        Count,
    };
    /// The common start of a draw: the pass (a new one unless the open encoder began with
    /// `state`), the pipeline, the dynamic state, the vertex buffers and the resources. False:
    /// the draw is dropped (counted); `forced` is a reason the caller found before.
    /// The pipeline state of `pipeline` that matches the attachments of `state`.
    const RenderPipeline& PipelineForPass(const GraphicsPipeline& pipeline,
                                          const RenderState& state);
    /// Begins (or continues) the render pass of `state` for `pipeline`'s draws; returns the state
    /// of the pass the draw goes into (an open pass with a depth buffer the draw leaves alone may
    /// take it, P6.2).
    const RenderState& OpenPass(const GraphicsPipeline& pipeline, const RenderState& state);
    bool SetupDraw(const GraphicsPipeline& pipeline, const RenderState& state,
                   const DynamicDrawState& dynamic, bool is_indexed, Drop forced);
    /// A tessellated direct draw (mtl_tessellation.h): the vertex and control kernels in a
    /// compute encoder, then the render pass of `state` (loading, not clearing, whatever the
    /// kernels interrupted) with the evaluation shader's patch draw.
    struct VelocityDraw {
        const RenderPipeline* pipeline{};
        RenderState guest{}, mirror{};
        DynamicDrawState dynamic{};
    };
    std::optional<VelocityDraw> PrepareVelocity(const GraphicsPipeline&, const RenderState&,
                                               const DynamicDrawState&);
    void ReplayVelocity(const GraphicsPipeline&, const DrawCall&, const VelocityDraw&);
    bool BypassMotionBlur(const GraphicsPipeline&, const RenderState&, const DynamicDrawState&);
    void RecordTessellationDraw(const GraphicsPipeline& pipeline, const RenderState& state,
                                const DrawCall& draw, const DynamicDrawState& dynamic);
    /// A geometry direct draw (mtl_geometry.h): per chunk of primitives the export and geometry
    /// kernels in a compute encoder, then the render pass of `state` with the strip draws.
    void RecordGeometryDraw(const GraphicsPipeline& pipeline, const RenderState& state,
                            const DrawCall& draw, const DynamicDrawState& dynamic);
    /// A geometry indirect draw (P7.1b, mtl_geometry.h "Indirect draws"): the guest's arguments
    /// are read on the GPU, one scratch span of a fixed budget per draw.
    void RecordGeometryDrawIndirect(const GraphicsPipeline& pipeline, const RenderState& state,
                                    bool is_indexed, const IndirectDraw& indirect,
                                    const IndirectBuffers& buffers, const DynamicDrawState& dynamic);
    /// The export kernel's vertex streams of a geometry draw (as SetupDraw resolves the render
    /// pipeline's) into `out[0 .. count)`; a reason to drop the draw, or None.
    Drop ResolveGeometryStreams(const GeometryPipeline& geo, Tessellation::VertexBinding* out,
                                u32 count);
    void NoteDrop(Drop reason);
    void MaybePrintStats();

    Device& device;
    Scheduler& scheduler;
    Runtime& runtime;
    std::unique_ptr<PipelineCache> pipeline_cache;
    /// Before the upscaler, which reads it (destroyed after it).
    std::unique_ptr<CameraMotion> camera_motion;
    /// The reduced scene targets of the live-resolution path (P7.5) and their interface for the
    /// core; before the upscaler, which draws from them (destroyed after it).
    std::unique_ptr<SceneTargets> metal_scene_targets;
    std::unique_ptr<SceneTargetsAdapter> scene_adapter;
    /// After the core's texture cache it reads, destroyed before the scheduler it defers to.
    std::unique_ptr<Upscaler> metal_upscaler;
    std::unique_ptr<VelocityMirror> velocity_mirror;
    std::unique_ptr<Impl> impl;
};

} // namespace Metal
