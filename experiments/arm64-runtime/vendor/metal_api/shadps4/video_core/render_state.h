// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project

#pragma once

#include <array>
#include <cstring>
#include <type_traits>

#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"

// bbport (plan P4.1 R6): the description of a render pass the rasterizer core hands to its
// backend and compares for the render state memo. Moved from vk_scheduler.h; the view handle is an
// opaque backend value now (Vulkan: the VkImageView), enums stay vocabulary.

namespace VideoCore {

/// A backend's image view as an opaque value (Vulkan: the VkImageView handle); 0: none.
using NativeView = u64;

struct RenderAttachment {
    NativeView image_view;
    vk::ImageLayout image_layout;
    std::array<u32, 4> clear_value;
    union {
        u32 is_clear;
        struct {
            bool has_depth;
            bool depth_clear;
            bool has_stencil;
            bool stencil_clear;
        };
    };
};
static_assert(std::has_unique_object_representations_v<RenderAttachment>);

struct RenderState {
    std::array<RenderAttachment, 8> color_attachments;
    RenderAttachment depth_stencil_attachment;
    u16 width;
    u16 height;
    u16 num_layers;
    u16 num_color_attachments;

    bool operator==(const RenderState& other) const noexcept {
        return std::memcmp(this, &other, sizeof(RenderState)) == 0;
    }
};
static_assert(std::has_unique_object_representations_v<RenderState>);

struct StencilOps {
    vk::StencilOp fail_op{};
    vk::StencilOp pass_op{};
    vk::StencilOp depth_fail_op{};
    vk::CompareOp compare_op{};

    bool operator==(const StencilOps& other) const {
        return fail_op == other.fail_op && pass_op == other.pass_op &&
               depth_fail_op == other.depth_fail_op && compare_op == other.compare_op;
    }
};

} // namespace VideoCore
