// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <unordered_map>

#include "video_core/cache_backend.h"
#include "video_core/renderer_vulkan/vk_common.h"

namespace Vulkan {

class Instance;
class Scheduler;

/// The Vulkan implementation of CacheBackend::DispatchTiling: SPIR-V compute pipelines per
/// TilingKey, recorded with push descriptors.
class TileKernels {
public:
    explicit TileKernels(const Instance& instance, Scheduler& scheduler);
    ~TileKernels();

    void Dispatch(const VideoCore::TilingDispatch& dispatch);

private:
    vk::Pipeline GetTilingPipeline(const VideoCore::TilingDispatch& dispatch);

private:
    const Instance& instance;
    Scheduler& scheduler;
    vk::UniqueDescriptorSetLayout desc_layout;
    vk::UniquePipelineLayout pl_layout;
    std::unordered_map<VideoCore::TilingKey, vk::UniquePipeline, VideoCore::TilingKey::Hash>
        tiling_pipelines;
};

} // namespace Vulkan
