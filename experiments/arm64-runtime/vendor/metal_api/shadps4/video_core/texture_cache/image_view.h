// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <utility>

#include "video_core/amdgpu/regs_depth.h"
#include "video_core/amdgpu/resource.h"
#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/texture_cache/types.h"

namespace AmdGpu {
struct ColorBuffer;
}

namespace Shader {
struct ImageResource;
}

namespace VideoCore {

class CacheBackend;

bool IsViewTypeCompatible(AmdGpu::ImageType view_type, AmdGpu::ImageType image_type);

struct ImageViewInfo {
    ImageViewInfo() = default;
    ImageViewInfo(const AmdGpu::Image& image, const Shader::ImageResource& desc) noexcept;
    ImageViewInfo(const AmdGpu::ColorBuffer& col_buffer) noexcept;
    ImageViewInfo(const AmdGpu::DepthBuffer& depth_buffer, AmdGpu::DepthView view,
                  AmdGpu::DepthControl ctl);

    AmdGpu::ImageType type = AmdGpu::ImageType::Color2D;
    vk::Format format = vk::Format::eR8G8B8A8Unorm;
    SubresourceRange range;
    vk::ComponentMapping mapping{};
    u32 min_lod = 0;
    bool is_storage = false;

    auto operator<=>(const ImageViewInfo&) const = default;
};

struct Image;

struct ImageView {
    ImageView(CacheBackend& backend, const ImageViewInfo& info, const Image& image);
    ~ImageView();

    ImageView(const ImageView&) = delete;
    ImageView& operator=(const ImageView&) = delete;

    ImageView(ImageView&& other) noexcept
        : info{other.info}, owner{other.owner}, native{std::exchange(other.native, nullptr)},
          image_view{std::move(other.image_view)} {}
    ImageView& operator=(ImageView&& other) noexcept;

    ImageViewInfo info;
    /// The backend that created the view (CacheBackend::CreateImageView sets it), null if none.
    CacheBackend* owner{};
    /// The backend's own object (a retained MTLTexture on Metal); null on Vulkan.
    void* native{};
    vk::UniqueImageView image_view;
};

} // namespace VideoCore
