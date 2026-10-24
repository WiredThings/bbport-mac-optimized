// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include "bbport_overlay.h"
#include "video_core/renderer_vulkan/vk_instance.h" // before imgui_impl_vulkan.h: vk_common.h's macros
#include "video_core/renderer_vulkan/vk_overlay.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "imgui.h"
#include "imgui_impl_vulkan.h"

namespace Vulkan::Overlay {

void Init(const Instance& instance, vk::Format format, u32 image_count) {
    BbOverlay::Init([&]() {
        const vk::Instance vk_instance = instance.GetInstance();
        ImGui_ImplVulkan_LoadFunctions(
            instance.ApiVersion(),
            [](const char* name, void* user) {
                return VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr(
                    *static_cast<const vk::Instance*>(user), name);
            },
            const_cast<vk::Instance*>(&vk_instance));

        const VkFormat color_format = static_cast<VkFormat>(format);
        ImGui_ImplVulkan_InitInfo info{};
        info.ApiVersion = instance.ApiVersion();
        info.Instance = vk_instance;
        info.PhysicalDevice = instance.GetPhysicalDevice();
        info.Device = instance.GetDevice();
        info.QueueFamily = instance.GetGraphicsQueueFamilyIndex();
        info.Queue = instance.GetGraphicsQueue();
        info.DescriptorPoolSize = 16;
        info.MinImageCount = std::max(image_count, 2u);
        info.ImageCount = std::max(image_count, 2u);
        info.UseDynamicRendering = true;
        info.PipelineInfoMain.PipelineRenderingCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &color_format,
        };
        return ImGui_ImplVulkan_Init(&info);
    });
}

void Render(vk::CommandBuffer cmdbuf, vk::ImageView view, vk::Extent2D extent) {
    BbOverlay::Render(
        extent.width, extent.height,
        BbOverlay::Backend{
            .new_frame = [] { ImGui_ImplVulkan_NewFrame(); },
            .render =
                [&](ImDrawData* draw_data) {
                    const vk::RenderingAttachmentInfo attachment{
                        .imageView = view,
                        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
                        .loadOp = vk::AttachmentLoadOp::eLoad,
                        .storeOp = vk::AttachmentStoreOp::eStore,
                    };
                    cmdbuf.beginRendering(vk::RenderingInfo{
                        .renderArea = {{0, 0}, extent},
                        .layerCount = 1,
                        .colorAttachmentCount = 1,
                        .pColorAttachments = &attachment,
                    });
                    {
                        // Font atlas uploads submit to the graphics queue themselves.
                        std::scoped_lock submit_lock{Scheduler::submit_mutex};
                        ImGui_ImplVulkan_RenderDrawData(draw_data, cmdbuf);
                    }
                    cmdbuf.endRendering();
                },
        });
}

} // namespace Vulkan::Overlay
