// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <utility>

#include "video_core/amdgpu/regs_texture.h"
#include "video_core/amdgpu/resource.h"
#include "video_core/renderer_vulkan/vk_common.h"

namespace VideoCore {

class CacheBackend;

/// bbport: a sampler as the backends bind it, by value: the samplers live in a map that moves
/// them when it grows, their Vulkan handle and native object do not move.
struct SamplerRef {
    vk::Sampler handle; ///< Vulkan
    void* native;       ///< the backend's own object (a retained MTLSamplerState on Metal)
};

class Sampler {
public:
    explicit Sampler(CacheBackend& backend, const AmdGpu::Sampler& sampler,
                     const AmdGpu::BorderColorBuffer border_color_base, bool is_depth,
                     float extra_lod_bias = 0.0f);
    ~Sampler();

    Sampler(const Sampler&) = delete;
    Sampler& operator=(const Sampler&) = delete;

    Sampler(Sampler&& other) noexcept
        : lru_id{other.lru_id}, owner{other.owner}, native{std::exchange(other.native, nullptr)},
          handle{std::move(other.handle)} {}
    Sampler& operator=(Sampler&& other) noexcept;

    vk::Sampler Handle() const noexcept {
        return *handle;
    }

    size_t lru_id{};

    /// The backend that created the sampler (CacheBackend::CreateSampler sets it), null if none.
    CacheBackend* owner{};
    /// The backend's own object (a retained MTLSamplerState on Metal); null on Vulkan.
    void* native{};
    vk::UniqueSampler handle;
};

} // namespace VideoCore
