// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "video_core/renderer_vulkan/vk_fault_parser.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_platform.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"

#include "video_core/host_shaders/fault_buffer_process_comp.h"

namespace Vulkan {

FaultParser::FaultParser(const Instance& instance, Scheduler& scheduler_, u32 page_bits,
                         u32 max_faults)
    : scheduler{scheduler_} {
    const auto device = instance.GetDevice();

    const std::array<vk::DescriptorSetLayoutBinding, 2> bindings = {{
        {
            .binding = 0,
            .descriptorType = vk::DescriptorType::eStorageBuffer,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eCompute,
        },
        {
            .binding = 1,
            .descriptorType = vk::DescriptorType::eStorageBuffer,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eCompute,
        },
    }};
    const vk::DescriptorSetLayoutCreateInfo desc_layout_ci = {
        .flags = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,
        .bindingCount = 2,
        .pBindings = bindings.data(),
    };
    desc_layout = Check(device.createDescriptorSetLayoutUnique(desc_layout_ci));

    const std::array<u32, 2> spec_data{page_bits, max_faults};
    const std::array<vk::SpecializationMapEntry, 2> spec_entries{{
        {0, 0, sizeof(u32)},
        {1, sizeof(u32), sizeof(u32)},
    }};
    const vk::SpecializationInfo specialization{
        .mapEntryCount = static_cast<u32>(spec_entries.size()),
        .pMapEntries = spec_entries.data(),
        .dataSize = sizeof(spec_data),
        .pData = spec_data.data(),
    };
    const auto module = CompileSPV(FAULT_BUFFER_PROCESS_COMP, device);
    SetObjectName(device, module, "Fault Buffer Parser");

    const vk::PipelineShaderStageCreateInfo shader_ci = {
        .stage = vk::ShaderStageFlagBits::eCompute,
        .module = module,
        .pName = "main",
        .pSpecializationInfo = &specialization,
    };

    const vk::PipelineLayoutCreateInfo layout_info = {
        .setLayoutCount = 1U,
        .pSetLayouts = &(*desc_layout),
    };
    pipeline_layout = Check(device.createPipelineLayoutUnique(layout_info));

    const vk::ComputePipelineCreateInfo pipeline_info = {
        .stage = shader_ci,
        .layout = *pipeline_layout,
    };
    pipeline = Check(device.createComputePipelineUnique({}, pipeline_info));
    SetObjectName(device, *pipeline, "Fault Buffer Parser Pipeline");

    device.destroyShaderModule(module);
}

FaultParser::~FaultParser() = default;

void FaultParser::Dispatch(const VideoCore::FaultParseDispatch& dispatch) {
    const vk::BufferMemoryBarrier2 pre_barrier = {
        .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
        .srcAccessMask = vk::AccessFlagBits2::eShaderWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
        .buffer = dispatch.fault_buffer->Handle(),
        .offset = 0,
        .size = dispatch.fault_buffer_size,
    };
    const vk::BufferMemoryBarrier2 post_barrier = {
        .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .srcAccessMask = vk::AccessFlagBits2::eShaderWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
        .dstAccessMask = vk::AccessFlagBits2::eShaderWrite,
        .buffer = dispatch.fault_buffer->Handle(),
        .offset = 0,
        .size = dispatch.fault_buffer_size,
    };
    const vk::DescriptorBufferInfo fault_buffer_info = {
        .buffer = dispatch.fault_buffer->Handle(),
        .offset = 0,
        .range = dispatch.fault_buffer_size,
    };
    const vk::DescriptorBufferInfo download_info = {
        .buffer = dispatch.download.buffer->Handle(),
        .offset = dispatch.download.offset,
        .range = dispatch.download.range,
    };
    const std::array<vk::WriteDescriptorSet, 2> writes = {{
        {
            .dstSet = VK_NULL_HANDLE,
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eStorageBuffer,
            .pBufferInfo = &fault_buffer_info,
        },
        {
            .dstSet = VK_NULL_HANDLE,
            .dstBinding = 1,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eStorageBuffer,
            .pBufferInfo = &download_info,
        },
    }};
    scheduler.EndRendering();
    const auto cmdbuf = scheduler.CommandBuffer();
    const Breadcrumbs::Scope crumb{cmdbuf, scheduler.CrumbStream(), "fault buffer"};
    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .dependencyFlags = vk::DependencyFlagBits::eByRegion,
        .bufferMemoryBarrierCount = 1,
        .pBufferMemoryBarriers = &pre_barrier,
    });
    cmdbuf.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
    cmdbuf.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, *pipeline_layout, 0, writes);
    cmdbuf.dispatch(dispatch.group_count_x, 1, 1);

    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .dependencyFlags = vk::DependencyFlagBits::eByRegion,
        .bufferMemoryBarrierCount = 1,
        .pBufferMemoryBarriers = &post_barrier,
    });
}

} // namespace Vulkan
