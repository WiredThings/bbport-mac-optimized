// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include "video_core/pipeline_cache_core.h"
#include "video_core/renderer_vulkan/vk_compute_pipeline.h"
#include "video_core/renderer_vulkan/vk_graphics_pipeline.h"
#include "video_core/renderer_vulkan/vk_resource_pool.h"

namespace Vulkan {

class Instance;
class Scheduler;

// bbport (plan P4.1 R5): these moved to the backend-neutral VideoCore::PipelineCacheCore
// (video_core/pipeline_cache_core.h); the Vulkan names stay.
using VideoCore::DrawIndirectParams;
using VideoCore::PipelineSelection;
using VideoCore::PrepWorker;
using VideoCore::Program;

struct PreparedDraw;

/// The Vulkan half of the pipeline cache: shader modules, pipelines and the driver's pipeline
/// cache. Key building, program selection and the disk cache are in the core.
class PipelineCache final : public VideoCore::PipelineCacheCore {
public:
    explicit PipelineCache(const Instance& instance, Scheduler& scheduler,
                           AmdGpu::Liverpool* liverpool, u32 sparse_page_shift);
    ~PipelineCache() override;

    /// `indirect`: the draw's arguments are in memory (bbport: never skipped while compiling).
    const GraphicsPipeline* GetGraphicsPipeline(const DrawIndirectParams params = {},
                                                const PreparedDraw* prepared = nullptr,
                                                bool indirect = false) {
        return static_cast<const GraphicsPipeline*>(
            PipelineCacheCore::GetGraphicsPipeline(params, prepared, indirect));
    }

    /// bbport: GPU-thread side: the pipeline for a prepared draw after checking that registers
    /// and flattened user data match; null to take the regular path.
    const GraphicsPipeline* TryPreparedPipeline(const PreparedDraw& prepared) {
        return static_cast<const GraphicsPipeline*>(
            PipelineCacheCore::TryPreparedPipeline(prepared));
    }

    const ComputePipeline* GetComputePipeline() {
        return static_cast<const ComputePipeline*>(PipelineCacheCore::GetComputePipeline());
    }

private:
    VideoCore::ShaderModuleHandle CreateShaderModule(std::span<const u32> spirv,
                                                     std::string_view name,
                                                     bool from_cache) override;
    void DestroyShaderModule(VideoCore::ShaderModuleHandle module) override;
    void CollectShader(const std::string& name, Shader::SwStage stage,
                       VideoCore::ShaderModuleHandle module, std::span<const u32> spirv,
                       std::span<const u32> code, std::span<const u32> patch,
                       bool is_patched) override;
    std::unique_ptr<VideoCore::PipelineSerialization> NewGraphicsSerialization() override;
    std::unique_ptr<VideoCore::PipelineSerialization> NewComputeSerialization() override;
    std::unique_ptr<VideoCore::PipelineBase> CreateGraphicsPipeline(
        const GraphicsPipelineKey& key, std::span<const Shader::Info*, MaxShaderStages> infos,
        std::span<const Shader::RuntimeInfo, MaxShaderStages> runtime_infos,
        std::optional<const Shader::Gcn::FetchShaderData> fetch_shader,
        std::span<const VideoCore::ShaderModuleHandle, MaxShaderStages> modules,
        VideoCore::PipelineSerialization& sdata, bool preloading) override;
    std::unique_ptr<VideoCore::PipelineBase> CreateComputePipeline(
        ComputePipelineKey key, const Shader::Info& info, VideoCore::ShaderModuleHandle module,
        VideoCore::PipelineSerialization& sdata, bool preloading) override;

private:
    const Instance& instance;
    Scheduler& scheduler;
    DescriptorHeap desc_heap;
    vk::UniquePipelineCache pipeline_cache;
    vk::UniquePipelineLayout pipeline_layout;
};

} // namespace Vulkan
