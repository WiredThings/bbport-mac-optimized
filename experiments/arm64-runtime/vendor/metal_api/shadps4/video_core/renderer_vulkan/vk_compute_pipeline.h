// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "video_core/pipeline_cache_core_types.h"
#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/renderer_vulkan/vk_pipeline_common.h"

namespace VideoCore {
class BufferCache;
class TextureCache;
} // namespace VideoCore

namespace Serialization {
struct Archive;
}

namespace Vulkan {

class Instance;
class Scheduler;
class DescriptorHeap;

using VideoCore::ComputePipelineKey;

class ComputePipeline : public Pipeline {
public:
    struct SerializationSupport final : VideoCore::PipelineSerialization {
        u32 dummy{};

        void Serialize(Serialization::Archive& ar) const override;
        bool Deserialize(Serialization::Archive& ar) override;
    };

    ComputePipeline(const Instance& instance, Scheduler& scheduler, DescriptorHeap& desc_heap,
                    const Shader::Profile& profile, vk::PipelineCache pipeline_cache,
                    ComputePipelineKey compute_key, const Shader::Info& info,
                    vk::ShaderModule module, SerializationSupport& sdata, bool preloading);
    ~ComputePipeline();

private:
    ComputePipelineKey compute_key;
};

} // namespace Vulkan
