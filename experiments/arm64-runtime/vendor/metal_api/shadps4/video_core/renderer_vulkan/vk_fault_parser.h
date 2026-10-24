// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "video_core/cache_backend.h"
#include "video_core/renderer_vulkan/vk_common.h"

namespace Vulkan {

class Instance;
class Scheduler;

/// The Vulkan implementation of CacheBackend::ParseFaultBuffer: one SPIR-V compute pipeline,
/// recorded with push descriptors between two buffer barriers.
class FaultParser {
public:
    explicit FaultParser(const Instance& instance, Scheduler& scheduler, u32 page_bits,
                         u32 max_faults);
    ~FaultParser();

    void Dispatch(const VideoCore::FaultParseDispatch& dispatch);

private:
    Scheduler& scheduler;
    vk::UniqueDescriptorSetLayout desc_layout;
    vk::UniquePipeline pipeline;
    vk::UniquePipelineLayout pipeline_layout;
};

} // namespace Vulkan
