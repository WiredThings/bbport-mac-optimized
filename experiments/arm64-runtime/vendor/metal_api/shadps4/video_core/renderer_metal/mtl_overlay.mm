// SPDX-License-Identifier: GPL-2.0-or-later
// Counterpart: renderer_vulkan/vk_overlay.cpp.

#include "bbport_overlay.h"
#include "imgui.h"
#include "imgui_impl_metal.h"
#include "video_core/renderer_metal/mtl_device_impl.h"
#include "video_core/renderer_metal/mtl_overlay.h"

namespace Metal::Overlay {

void Init(Device& device) {
    BbOverlay::Init([&] { return ImGui_ImplMetal_Init(device.GetImpl().device); });
}

void Render(const void* command_buffer, const void* texture, const void* borrowed_encoder) {
    id<MTLCommandBuffer> cmdbuf = (__bridge id<MTLCommandBuffer>)command_buffer;
    id<MTLTexture> target = (__bridge id<MTLTexture>)texture;

    // The backend keeps the pass's pixel formats (its pipeline cache key) from NewFrame, so the
    // descriptor is made before the frame and the encoder is created from it after.
    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = target;
    pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;

    BbOverlay::Render(
        u32(target.width), u32(target.height),
        BbOverlay::Backend{
            .new_frame = [&] { ImGui_ImplMetal_NewFrame(pass); },
            .render =
                [&](ImDrawData* draw_data) {
                    id<MTLRenderCommandEncoder> encoder = borrowed_encoder
                        ? (__bridge id<MTLRenderCommandEncoder>)borrowed_encoder
                        : [cmdbuf renderCommandEncoderWithDescriptor:pass];
                    if (!borrowed_encoder) encoder.label = @"Overlay";
                    // Font atlas creation and updates happen in here (the backend has
                    // ImGuiBackendFlags_RendererHasTextures; it uploads synchronously).
                    ImGui_ImplMetal_RenderDrawData(draw_data, cmdbuf, encoder);
                    if (!borrowed_encoder) [encoder endEncoding];
                },
        });
}

} // namespace Metal::Overlay
