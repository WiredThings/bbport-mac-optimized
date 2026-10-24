// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the Vulkan side of the in-game overlay (shim/bbport_overlay.h): Dear ImGui's Vulkan
// backend, drawn with dynamic rendering into the swapchain image after the game frame.

#pragma once

#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"

namespace Vulkan {

class Instance;

namespace Overlay {

/// Present thread, once: the ImGui context and its Vulkan backend.
void Init(const Instance& instance, vk::Format format, u32 image_count);

/// Present thread: draws into `view` (layout ColorAttachmentOptimal) when the overlay is visible.
void Render(vk::CommandBuffer cmdbuf, vk::ImageView view, vk::Extent2D extent);

} // namespace Overlay

} // namespace Vulkan
