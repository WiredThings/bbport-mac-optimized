// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cstdlib>

#include "core/debug_state.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"

namespace Vulkan {

constexpr static auto SpirvVersion1_6 = 0x00010600U;

constexpr static std::array DescriptorHeapSizes = {
    vk::DescriptorPoolSize{vk::DescriptorType::eUniformBuffer, 512},
    vk::DescriptorPoolSize{vk::DescriptorType::eStorageBuffer, 8192},
    vk::DescriptorPoolSize{vk::DescriptorType::eSampledImage, 8192},
    vk::DescriptorPoolSize{vk::DescriptorType::eStorageImage, 1024},
    vk::DescriptorPoolSize{vk::DescriptorType::eSampler, 1024},
};

using VideoCore::ShaderModuleHandle;

static ShaderModuleHandle ToHandle(vk::ShaderModule module) {
    return ShaderModuleHandle{reinterpret_cast<u64>(static_cast<VkShaderModule>(module))};
}

static vk::ShaderModule ToModule(ShaderModuleHandle handle) {
    return vk::ShaderModule{reinterpret_cast<VkShaderModule>(handle.value)};
}

/// The device-dependent fields of the shader profile; the core adds the neutral ones.
static Shader::Profile MakeProfile(const Instance& instance) {
    const auto& vk12_props = instance.GetVk12Properties();
    return Shader::Profile{
        .max_viewport_width = instance.GetMaxViewportWidth(),
        .max_viewport_height = instance.GetMaxViewportHeight(),
        .max_shared_memory_size = instance.MaxComputeSharedMemorySize(),
        .supported_spirv = SpirvVersion1_6,
        .subgroup_size = instance.SubgroupSize(),
        .support_int8 = instance.IsShaderInt8Supported(),
        .support_int16 = instance.IsShaderInt16Supported(),
        .support_int64 = instance.IsShaderInt64Supported(),
        .support_float16 = instance.IsShaderFloat16Supported(),
        .support_float64 = instance.IsShaderFloat64Supported(),
        .supports_denorm_behavior_independence =
            vk12_props.denormBehaviorIndependence != vk::ShaderFloatControlsIndependence::eNone,
        .supports_rounding_mode_independence =
            vk12_props.roundingModeIndependence != vk::ShaderFloatControlsIndependence::eNone,
        .support_fp16_denorm_preserve = bool(vk12_props.shaderDenormPreserveFloat16),
        .support_fp16_denorm_flush = bool(vk12_props.shaderDenormFlushToZeroFloat16),
        .support_fp16_round_to_zero = bool(vk12_props.shaderRoundingModeRTZFloat16),
        .support_fp32_denorm_preserve = bool(vk12_props.shaderDenormPreserveFloat32),
        .support_fp32_denorm_flush = bool(vk12_props.shaderDenormFlushToZeroFloat32),
        .support_fp32_round_to_zero = bool(vk12_props.shaderRoundingModeRTZFloat32),
        .support_fp64_denorm_preserve = bool(vk12_props.shaderDenormPreserveFloat64),
        .support_fp64_denorm_flush = bool(vk12_props.shaderDenormFlushToZeroFloat64),
        .support_fp64_round_to_zero = bool(vk12_props.shaderRoundingModeRTZFloat64),
        .support_fp16_signed_zero_inf_nan_preserve =
            bool(vk12_props.shaderSignedZeroInfNanPreserveFloat16),
        .support_fp32_signed_zero_inf_nan_preserve =
            bool(vk12_props.shaderSignedZeroInfNanPreserveFloat32),
        .support_fp64_signed_zero_inf_nan_preserve =
            bool(vk12_props.shaderSignedZeroInfNanPreserveFloat64),
        .supports_image_load_store_lod = instance.IsImageLoadStoreLodSupported(),
        .supports_native_cube_calc = instance.IsAmdGcnShaderSupported(),
        .supports_trinary_minmax = instance.IsAmdShaderTrinaryMinMaxSupported(),
        .supports_buffer_fp32_atomic_min_max =
            instance.IsShaderAtomicFloatBuffer32MinMaxSupported(),
        .supports_image_fp32_atomic_min_max = instance.IsShaderAtomicFloatImage32MinMaxSupported(),
        .supports_buffer_int64_atomics = instance.IsBufferInt64AtomicsSupported(),
        .supports_shared_int64_atomics = instance.IsSharedInt64AtomicsSupported(),
        .supports_workgroup_explicit_memory_layout =
            instance.IsWorkgroupMemoryExplicitLayoutSupported(),
        .supports_amd_shader_explicit_vertex_parameter =
            instance.IsAmdShaderExplicitVertexParameterSupported(),
        .supports_fragment_shader_barycentric = instance.IsFragmentShaderBarycentricSupported(),
        .supports_shader_subgroup_clock = instance.IsShaderSubgroupClockSupported(),
        .needs_manual_interpolation = instance.IsFragmentShaderBarycentricSupported() &&
                                      instance.GetDriverID() == vk::DriverId::eNvidiaProprietary,
        // bbport: older NVIDIA (Pascal) has no barycentrics; BB_INTERP_INT_FIX=0/1 overrides.
        .needs_integer_interpolation_fix = [&] {
            if (const char* env = std::getenv("BB_INTERP_INT_FIX")) {
                return env[0] == '1';
            }
            return !instance.IsFragmentShaderBarycentricSupported() &&
                   instance.GetDriverID() == vk::DriverId::eNvidiaProprietary;
        }(),
        .needs_lds_barriers = instance.GetDriverID() == vk::DriverId::eNvidiaProprietary ||
                              instance.GetDriverID() == vk::DriverId::eMesaKosmickrisp,
        .needs_buffer_offsets = instance.StorageMinAlignment() > 4,
        .needs_unorm_fixup = instance.GetDriverID() == vk::DriverId::eMesaKosmickrisp,
        .needs_clip_distance_emulation = instance.GetDriverID() == vk::DriverId::eNvidiaProprietary,
        .supports_shader_stencil_export = instance.IsShaderStencilExportSupported(),
    };
}

static VideoCore::PipelineCapabilities MakeCapabilities(const Instance& instance) {
    return {
        .depth_clip_control = instance.IsDepthClipControlSupported(),
        .vertex_input_dynamic_state = instance.IsVertexInputDynamicState(),
        .geometry_stage = instance.IsGeometryStageSupported(),
        .tessellation = instance.IsTessellationSupported(),
        .mixed_any_samples = instance.IsMixedAnySamplesSupported(),
        .mixed_depth_samples = instance.IsMixedDepthSamplesSupported(),
    };
}

PipelineCache::PipelineCache(const Instance& instance_, Scheduler& scheduler_,
                             AmdGpu::Liverpool* liverpool_, u32 sparse_page_shift)
    : PipelineCacheCore{liverpool_, MakeProfile(instance_), MakeCapabilities(instance_),
                        sparse_page_shift},
      instance{instance_}, scheduler{scheduler_},
      desc_heap{instance, scheduler.GetWorkSemaphore(), DescriptorHeapSizes} {
    WarmUp();

    auto [cache_result, cache] = instance.GetDevice().createPipelineCacheUnique({});
    ASSERT_MSG(cache_result == vk::Result::eSuccess, "Failed to create pipeline cache: {}",
               vk::to_string(cache_result));
    pipeline_cache = std::move(cache);
}

PipelineCache::~PipelineCache() {
    // The compile threads and the pipelines use this class' members.
    DestroyPipelines();
}

ShaderModuleHandle PipelineCache::CreateShaderModule(std::span<const u32> spirv,
                                                     std::string_view name, bool from_cache) {
    vk::ShaderModule module;
    if (from_cache) {
        auto [result, created] = instance.GetDevice().createShaderModule(
            {.codeSize = spirv.size() * sizeof(u32), .pCode = spirv.data()});
        if (result != vk::Result::eSuccess) {
            return {};
        }
        module = created;
    } else {
        module = CompileSPV(spirv, instance.GetDevice());
        if (!name.empty()) {
            Vulkan::SetObjectName(instance.GetDevice(), module, std::string{name});
        }
    }
    return ToHandle(module);
}

void PipelineCache::DestroyShaderModule(ShaderModuleHandle module) {
    instance.GetDevice().destroyShaderModule(ToModule(module));
}

void PipelineCache::CollectShader(const std::string& name, Shader::SwStage stage,
                                  ShaderModuleHandle module, std::span<const u32> spirv,
                                  std::span<const u32> code, std::span<const u32> patch,
                                  bool is_patched) {
    DebugState.CollectShader(name, stage, ToModule(module), spirv, code, patch, is_patched);
}

std::unique_ptr<VideoCore::PipelineSerialization> PipelineCache::NewGraphicsSerialization() {
    return std::make_unique<GraphicsPipeline::SerializationSupport>();
}

std::unique_ptr<VideoCore::PipelineSerialization> PipelineCache::NewComputeSerialization() {
    return std::make_unique<ComputePipeline::SerializationSupport>();
}

std::unique_ptr<VideoCore::PipelineBase> PipelineCache::CreateGraphicsPipeline(
    const GraphicsPipelineKey& key, std::span<const Shader::Info*, MaxShaderStages> infos,
    std::span<const Shader::RuntimeInfo, MaxShaderStages> runtime_infos,
    std::optional<const Shader::Gcn::FetchShaderData> fetch_shader,
    std::span<const ShaderModuleHandle, MaxShaderStages> modules,
    VideoCore::PipelineSerialization& sdata, bool preloading) {
    std::array<vk::ShaderModule, MaxShaderStages> vk_modules;
    std::ranges::transform(modules, vk_modules.begin(), ToModule);
    return std::make_unique<GraphicsPipeline>(
        instance, scheduler, desc_heap, profile, key, *pipeline_cache, infos, runtime_infos,
        std::move(fetch_shader), vk_modules,
        static_cast<GraphicsPipeline::SerializationSupport&>(sdata), preloading);
}

std::unique_ptr<VideoCore::PipelineBase> PipelineCache::CreateComputePipeline(
    ComputePipelineKey key, const Shader::Info& info, ShaderModuleHandle module,
    VideoCore::PipelineSerialization& sdata, bool preloading) {
    return std::make_unique<ComputePipeline>(
        instance, scheduler, desc_heap, profile, *pipeline_cache, key, info, ToModule(module),
        static_cast<ComputePipeline::SerializationSupport&>(sdata), preloading);
}

} // namespace Vulkan
