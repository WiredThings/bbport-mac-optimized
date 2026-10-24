// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <boost/container/static_vector.hpp>
#include <xxhash.h>

#include "shader_recompiler/frontend/fetch_shader.h"
#include "video_core/amdgpu/regs_color.h"
#include "video_core/amdgpu/regs_depth.h"
#include "video_core/amdgpu/regs_primitive.h"
#include "video_core/pipeline_cache_core_types.h"
#include "video_core/renderer_vulkan/vk_pipeline_common.h"

namespace VideoCore {
class BufferCache;
class TextureCache;
} // namespace VideoCore

namespace Vulkan {

using VideoCore::GraphicsPipelineKey;
using VideoCore::MaxShaderStages;
using VideoCore::MaxVertexBufferCount;
using VideoCore::VertexInputs;

class Instance;
class Scheduler;
class DescriptorHeap;

/// bbport: the blend state of color attachment `i` as the key asks for it, with the rewrites that
/// emulate GCN behaviour (min/max with a zeroing factor, the squared self-scaled output, alpha
/// masked out of the export). Shared by the Vulkan and Metal pipelines.
struct AttachmentBlend {
    bool enable{};
    vk::BlendFactor src_color{}, dst_color{}, src_alpha{}, dst_alpha{};
    vk::BlendOp color_op{}, alpha_op{};
    bool unimplemented_min_max{}; ///< min/max with factors other than one (not emulated)
    bool self_scaled{};           ///< the shader squares its output (blend_self_scale)
};
AttachmentBlend AttachmentBlendState(const GraphicsPipelineKey& key, u32 i);

class GraphicsPipeline : public Pipeline {
public:
    struct SerializationSupport final : VideoCore::PipelineSerialization {
        VertexInputs<vk::VertexInputAttributeDescription> vertex_attributes{};
        VertexInputs<vk::VertexInputBindingDescription> vertex_bindings{};
        VertexInputs<vk::VertexInputBindingDivisorDescriptionEXT> divisors{};
        vk::PipelineMultisampleStateCreateInfo multisampling{};
        std::vector<u32> tcs{};
        std::vector<u32> tes{};
        std::vector<u32> fragment{};

        void Serialize(Serialization::Archive& ar) const override;
        bool Deserialize(Serialization::Archive& ar) override;
    };

    GraphicsPipeline(const Instance& instance, Scheduler& scheduler, DescriptorHeap& desc_heap,
                     const Shader::Profile& profile, const GraphicsPipelineKey& key,
                     vk::PipelineCache pipeline_cache,
                     std::span<const Shader::Info*, MaxShaderStages> stages,
                     std::span<const Shader::RuntimeInfo, MaxShaderStages> runtime_infos,
                     std::optional<const Shader::Gcn::FetchShaderData> fetch_shader,
                     std::span<const vk::ShaderModule> modules, SerializationSupport& sdata,
                     bool preloading);
    ~GraphicsPipeline();

    // GetVertexInputs: VideoCore::PipelineBase.

private:
    void BuildDescSetLayout(bool preloading);
};

struct ClipDistanceShaderKey {
    std::array<std::tuple<u8, u8>, 8> clip_locations;

    bool operator==(const ClipDistanceShaderKey& key) const noexcept {
        return std::memcmp(this, &key, sizeof(key)) == 0;
    }
};

} // namespace Vulkan

template <>
struct std::hash<Vulkan::ClipDistanceShaderKey> {
    std::size_t operator()(const Vulkan::ClipDistanceShaderKey& key) const noexcept {
        return XXH3_64bits(&key, sizeof(key));
    }
};
