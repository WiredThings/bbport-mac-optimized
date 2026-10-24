// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the Metal side of the in-game overlay (shim/bbport_overlay.h): Dear ImGui's Metal
// backend (imgui_impl_metal.mm), drawn into the drawable after the game frame. Counterpart:
// renderer_vulkan/vk_overlay.h.

#pragma once

namespace Metal {

class Device;

namespace Overlay {

/// Present thread, once (the presenter's constructor): the ImGui context and its Metal backend.
void Init(Device& device);

/// Present thread, in Present after the frame is drawn into the drawable and before
/// presentDrawable: when the overlay is visible, encodes a render pass onto `texture` (load,
/// store) in `command_buffer`. Both are bridged Objective-C pointers, `id<MTLCommandBuffer>` and
/// `id<MTLTexture>` (the drawable's texture, BGRA8Unorm).
// Optional borrowed encoder: same single-sample BGRA8 drawable, still open after this call.
// ImGui establishes its full viewport, scissor and pipeline; caller owns endEncoding.
void Render(const void* command_buffer, const void* texture, const void* borrowed_encoder = nullptr);

} // namespace Overlay

} // namespace Metal
