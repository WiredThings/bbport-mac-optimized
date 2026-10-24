// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <cstring>
#include <optional>
#include <span>

#include <boost/container/static_vector.hpp>
#include <xxhash.h>

#include "common/types.h"
#include "shader_recompiler/frontend/fetch_shader.h"
#include "shader_recompiler/runtime_info.h"
#include "video_core/amdgpu/regs_color.h"
#include "video_core/amdgpu/regs_depth.h"
#include "video_core/amdgpu/regs_primitive.h"
#include "video_core/renderer_vulkan/vk_common.h"

// bbport (plan P4.1 R5): the vocabulary of the pipeline cache that both backends share. The keys,
// moved here from vk_graphics_pipeline.h / vk_compute_pipeline.h (the Vulkan names stay as aliases
// there), and the neutral base of the pipelines a backend creates (PipelineBase).

namespace Serialization {
struct Archive;
}

namespace Shader {
struct Info;
}

namespace VideoCore {

static constexpr u32 MaxShaderStages = static_cast<u32>(Shader::SwStage::NumLogicalStages);
static constexpr u32 MaxVertexBufferCount = 32;

template <typename T>
using VertexInputs = boost::container::static_vector<T, MaxVertexBufferCount>;

struct GraphicsPipelineKey {
    std::array<size_t, MaxShaderStages> stage_hashes;
    std::array<vk::Format, MaxVertexBufferCount> vertex_buffer_formats;
    u32 patch_control_points;
    u32 num_color_attachments;
    std::array<Shader::PsColorBuffer, AmdGpu::NUM_COLOR_BUFFERS> color_buffers;
    std::array<AmdGpu::BlendControl, AmdGpu::NUM_COLOR_BUFFERS> blend_controls;
    std::array<vk::ColorComponentFlags, AmdGpu::NUM_COLOR_BUFFERS> write_masks;
    AmdGpu::ColorBufferMask cb_shader_mask;
    AmdGpu::ColorControl::LogicOp logic_op;
    u8 num_samples;
    u8 depth_samples;
    std::array<u8, AmdGpu::NUM_COLOR_BUFFERS> color_samples;
    u32 mrt_mask;
    u32 motion_vectors = 0;
    struct {
        AmdGpu::DepthBuffer::ZFormat z_format : 2;
        AmdGpu::DepthBuffer::StencilFormat stencil_format : 1;
        u32 depth_clamp_enable : 1;
    };
    struct {
        AmdGpu::PrimitiveType prim_type : 5;
        AmdGpu::PolygonMode polygon_mode : 2;
        AmdGpu::ClipSpace clip_space : 1;
        AmdGpu::ProvokingVtxLast provoking_vtx_last : 1;
        u32 depth_clip_enable : 1;
    };

    GraphicsPipelineKey() {
        std::memset(this, 0, sizeof(*this));
    }

    bool operator==(const GraphicsPipelineKey& key) const noexcept {
        return std::memcmp(this, &key, sizeof(key)) == 0;
    }

    void Serialize(Serialization::Archive& ar) const;
    bool Deserialize(Serialization::Archive& ar);
};

struct ComputePipelineKey {
    size_t value;

    friend bool operator==(const ComputePipelineKey& lhs, const ComputePipelineKey& rhs) {
        return lhs.value == rhs.value;
    }
    friend bool operator!=(const ComputePipelineKey& lhs, const ComputePipelineKey& rhs) {
        return !(lhs == rhs);
    }

    void Serialize(Serialization::Archive& ar) const;
    bool Deserialize(Serialization::Archive& ar);
};

/// An opaque per-backend handle of a compiled shader module (Vulkan: the VkShaderModule, Metal:
/// the CompiledShader). Zero is no module.
struct ShaderModuleHandle {
    u64 value{};

    explicit operator bool() const noexcept {
        return value != 0;
    }
    friend bool operator==(const ShaderModuleHandle&, const ShaderModuleHandle&) = default;
};

/// The backend's part of a pipeline's disk cache entry, written after the key (Vulkan: the vertex
/// input and multisample state). The core only carries it between the backend's hooks and the
/// archive.
struct PipelineSerialization {
    virtual ~PipelineSerialization() = default;
    virtual void Serialize(Serialization::Archive& ar) const = 0;
    virtual bool Deserialize(Serialization::Archive& ar) = 0;
};

/// What the neutral code reads of a pipeline a backend created: its stage infos, the fetch
/// shader and the key. Graphics-only members are empty for compute pipelines.
class PipelineBase {
public:
    virtual ~PipelineBase() = default;

    auto GetStages() const {
        static_assert(static_cast<u32>(Shader::SwStage::Compute) == Shader::MaxStageTypes - 1);
        if (is_compute) {
            return std::span{stages.cend() - 1, stages.cend()};
        } else {
            return std::span{stages.cbegin(), stages.cend() - 1};
        }
    }

    const Shader::Info& GetStage(Shader::SwStage stage) const noexcept {
        return *stages[u32(stage)];
    }

    bool IsCompute() const {
        return is_compute;
    }

    const std::optional<const Shader::Gcn::FetchShaderData>& GetFetchShader() const noexcept {
        return fetch_shader;
    }

    const GraphicsPipelineKey& GetGraphicsKey() const {
        return key;
    }

    /// Gets the attributes and bindings for vertex inputs (Vulkan vocabulary descriptions).
    /// bbport: `sharps`, when given, are the V#s of the fetch shader's attributes as the GPU
    /// command thread read them (in the draw packet): the V# tables they come from may have been
    /// rewritten by a later constant engine dump by the time the recording thread gets here.
    /// Defined in pipeline_cache_core.cpp for vk::VertexInput{Attribute,Binding}Description and
    /// the ...2EXT versions.
    template <typename Attribute, typename Binding>
    void GetVertexInputs(VertexInputs<Attribute>& attributes, VertexInputs<Binding>& bindings,
                         VertexInputs<vk::VertexInputBindingDivisorDescriptionEXT>& divisors,
                         VertexInputs<AmdGpu::Buffer>& guest_buffers, u32 step_rate_0,
                         u32 step_rate_1, std::span<const AmdGpu::Buffer> sharps = {}) const;

protected:
    explicit PipelineBase(bool is_compute_) : is_compute{is_compute_} {}
    PipelineBase(const GraphicsPipelineKey& key_,
                 std::optional<const Shader::Gcn::FetchShaderData> fetch_shader_)
        : key{key_}, fetch_shader{std::move(fetch_shader_)}, is_compute{false} {}

    std::array<const Shader::Info*, Shader::MaxStageTypes> stages{};
    GraphicsPipelineKey key;
    std::optional<const Shader::Gcn::FetchShaderData> fetch_shader{};
    bool is_compute;
};

} // namespace VideoCore

template <>
struct std::hash<VideoCore::GraphicsPipelineKey> {
    std::size_t operator()(const VideoCore::GraphicsPipelineKey& key) const noexcept {
        return XXH3_64bits(&key, sizeof(key));
    }
};

template <>
struct std::hash<VideoCore::ComputePipelineKey> {
    std::size_t operator()(const VideoCore::ComputePipelineKey& key) const noexcept {
        return std::hash<size_t>{}(key.value);
    }
};

template <>
struct std::hash<VideoCore::ShaderModuleHandle> {
    std::size_t operator()(const VideoCore::ShaderModuleHandle& module) const noexcept {
        return std::hash<u64>{}(module.value);
    }
};
