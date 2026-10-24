// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <boost/container/small_vector.hpp>
#include "bbport_guest_memory.h"
#include "bbport_toggles.h"
#include "video_core/buffer_cache/buffer.h"
#include "video_core/renderer_vulkan/liverpool_to_vk.h"
#include "video_core/renderer_vulkan/vk_blit_helper.h"
#include "video_core/renderer_vulkan/vk_fault_parser.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_platform.h"
#include "video_core/renderer_vulkan/vk_runtime.h"
#include "video_core/renderer_vulkan/vk_gpu_profiler.h"
#include "video_core/renderer_vulkan/vk_scene_resolution.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_sparse_arena.h"
#include "video_core/renderer_vulkan/vk_tile_kernels.h"
#include "video_core/texture_cache/image.h"

#include <vulkan/vulkan_format_traits.hpp>
#include <vk_mem_alloc.h>

namespace Vulkan {

static vk::ImageType ConvertImageType(AmdGpu::ImageType type) noexcept {
    switch (type) {
    case AmdGpu::ImageType::Color1D:
    case AmdGpu::ImageType::Color1DArray:
    case AmdGpu::ImageType::Color2D:
    case AmdGpu::ImageType::Color2DMsaa:
    case AmdGpu::ImageType::Color2DArray:
        return vk::ImageType::e2D;
    case AmdGpu::ImageType::Color3D:
        return vk::ImageType::e3D;
    default:
        UNREACHABLE_MSG("Unexpected image type {}", u32(type));
    }
}

static std::pair<u32, u32> SanitizeCopyLayers(const VideoCore::ImageInfo& src_info,
                                              const VideoCore::ImageInfo& dst_info,
                                              const u32 depth) {
    const auto vk_src_type = ConvertImageType(src_info.type);
    const auto vk_dst_type = ConvertImageType(dst_info.type);

    u32 src_layers = src_info.resources.layers;
    u32 dst_layers = dst_info.resources.layers;

    // 3D images can only use 1 layer.
    if (vk_src_type == vk::ImageType::e3D && src_layers != 1) {
        LOG_WARNING(Render_Vulkan, "Coercing copy 3D source layers {} to 1.", src_layers);
        src_layers = 1;
    }
    if (vk_dst_type == vk::ImageType::e3D && dst_layers != 1) {
        LOG_WARNING(Render_Vulkan, "Coercing copy 3D destination layers {} to 1.", dst_layers);
        dst_layers = 1;
    }

    // If the image type is equal, layer count must match. Take the minimum of both.
    if (vk_src_type == vk_dst_type) {
        if (src_layers != dst_layers) {
            LOG_WARNING(Render_Vulkan,
                        "Coercing copy source layers {} and destination layers {} to minimum.",
                        src_layers, dst_layers);
            src_layers = dst_layers = std::min(src_layers, dst_layers);
        }
    } else {
        // For 2D <-> 3D copies, 2D layer count must equal 3D depth.
        if (vk_src_type == vk::ImageType::e2D && vk_dst_type == vk::ImageType::e3D &&
            src_layers != depth) {
            LOG_WARNING(Render_Vulkan,
                        "Coercing copy 2D source layers {} to 3D destination depth {}", src_layers,
                        depth);
            src_layers = depth;
        }
        if (vk_src_type == vk::ImageType::e3D && vk_dst_type == vk::ImageType::e2D &&
            dst_layers != depth) {
            LOG_WARNING(Render_Vulkan,
                        "Coercing copy 2D destination layers {} to 3D source depth {}", dst_layers,
                        depth);
            dst_layers = depth;
        }
    }

    return std::make_pair(src_layers, dst_layers);
}

static u32 BufferImageCopySize(const vk::BufferImageCopy& copy, const vk::Format pixel_format) {
    const u32 row_length = copy.bufferRowLength ? copy.bufferRowLength : copy.imageExtent.width;
    const u32 height = copy.bufferImageHeight ? copy.bufferImageHeight : copy.imageExtent.height;

    const auto block = vk::blockExtent(pixel_format);
    const u32 block_size = vk::blockSize(pixel_format);
    const u32 row_pitch = (row_length / block[0]) * block_size;
    const u32 slice_pitch = (height / block[1]) * row_pitch;

    const u32 width_in_blocks = (copy.imageExtent.width + block[0] - 1) / block[0];
    const u32 height_in_blocks = (copy.imageExtent.height + block[1] - 1) / block[1];
    const u32 num_slices = copy.imageExtent.depth * copy.imageSubresource.layerCount;

    return (num_slices - 1) * slice_pitch + (height_in_blocks - 1) * row_pitch +
           width_in_blocks * block_size;
}

static VideoCore::BackendLimits MakeLimits(const Instance& instance) {
    return {
        .uniform_min_alignment = instance.UniformMinAlignment(),
        .storage_min_alignment = instance.StorageMinAlignment(),
        .non_coherent_atom_size = instance.NonCoherentAtomSize(),
        .max_sampler_anisotropy = instance.MaxSamplerAnisotropy(),
        .max_sampler_lod_bias = instance.MaxSamplerLodBias(),
        .max_sampler_allocation_count = instance.GetMaxSamplerAllocationCount(),
        .total_memory_budget = instance.GetTotalMemoryBudget(),
        .integrated = instance.IsIntegrated(),
        .can_report_memory_usage = instance.CanReportMemoryUsage(),
        .image_view_min_lod = instance.IsImageViewMinLodSupported(),
        .anisotropic_filtering = instance.IsAnisotropicFilteringSupported(),
        .custom_border_color = instance.IsCustomBorderColorSupported(),
        .image_2d_view_of_3d = instance.Is2dViewOf3dSupported(),
        .block_texel_view = instance.IsBlockTexelViewSupported(),
        .attachment_feedback_loop_layout = instance.IsAttachmentFeedbackLoopLayoutSupported(),
        .max_framebuffer_width = instance.GetMaxFramebufferWidth(),
        .max_framebuffer_height = instance.GetMaxFramebufferHeight(),
        .max_viewport_width = instance.GetMaxViewportWidth(),
        .max_viewport_height = instance.GetMaxViewportHeight(),
        .depth_range_unrestricted = instance.IsDepthRangeUnrestrictedSupported(),
        .list_restart = instance.IsListRestartSupported(),
        .patch_list_restart = instance.IsPatchListRestartSupported(),
        .vertex_input_dynamic_state = instance.IsVertexInputDynamicState(),
        .null_descriptor = instance.IsNullDescriptorSupported(),
    };
}

Runtime::Runtime(const Instance& instance_, Scheduler& scheduler_)
    : VideoCore::CacheBackend{MakeLimits(instance_)}, instance{instance_}, scheduler{scheduler_},
      staging_pool{instance_, scheduler_, *this} {
    blit_helper = std::make_unique<BlitHelper>(instance, scheduler, *this);
    tile_kernels = std::make_unique<TileKernels>(instance, scheduler);
    sparse_arena = std::make_unique<SparseArena>(instance, scheduler);

    memory_barrier.dstStageMask = vk::PipelineStageFlagBits2::eAllCommands;
    memory_barrier.dstAccessMask =
        vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite;
}

Runtime::~Runtime() = default;

void Runtime::TickFrame() {
    staging_pool.TickFrame();
}

VideoCore::StagingSpan Runtime::StagingRequest(u64 size, VideoCore::MemoryType type,
                                               u64 alignment, bool deferred) {
    return staging_pool.Request(size, type, alignment, deferred);
}

void Runtime::StagingFreeDeferred(const VideoCore::StagingSpan& span) {
    staging_pool.FreeDeferred(span);
}

u64 Runtime::CurrentTick() const noexcept {
    return scheduler.CurrentTick();
}

bool Runtime::IsFree(u64 tick) noexcept {
    return scheduler.IsFree(tick);
}

bool Runtime::IsFreeKnown(u64 tick) const noexcept {
    return scheduler.GetWorkSemaphore()->IsFree(tick);
}

void Runtime::RefreshTicks() {
    scheduler.GetWorkSemaphore()->Refresh();
}

void Runtime::Wait(u64 tick) {
    scheduler.Wait(tick);
}

void Runtime::Finish() {
    scheduler.Finish();
}

void Runtime::DeferOperation(Common::UniqueFunction<void>&& func) {
    scheduler.DeferOperation(std::move(func));
}

void Runtime::DeferPriorityOperation(Common::UniqueFunction<void>&& func) {
    scheduler.DeferPriorityOperation(std::move(func));
}

void Runtime::PopPendingOperations() {
    scheduler.PopPendingOperations();
}

void Runtime::EndRendering() {
    scheduler.EndRendering();
}

bool Runtime::IsRendering() const noexcept {
    return scheduler.IsRendering();
}

bool Runtime::IsRecordingDeferred() const noexcept {
    return scheduler.IsRecordingDeferred();
}

void Runtime::NoteHostCopySource(u64 address, u64 size) {
    scheduler.NoteHostCopySource(address, size);
}

void Runtime::RecordHostCopy(const BbCopy::Item& item) {
    scheduler.RecordHostCopy([item] { item.run(item); });
}

vk::Format Runtime::SupportedFormat(vk::Format format, vk::FormatFeatureFlags2 features) const {
    return instance.GetSupportedFormat(format, features);
}

u64 Runtime::DeviceMemoryUsage() const {
    return instance.GetDeviceMemoryUsage();
}

u64 Runtime::DeviceMemoryBudgetNow() const {
    return instance.GetDeviceMemoryBudgetNow();
}

void Runtime::CopyBuffer(const VideoCore::Buffer* src, const VideoCore::Buffer* dst,
                         std::span<const vk::BufferCopy> copies) {
    scheduler.EndRendering();

    // bbport: many regions (HLE copy shaders) are tracked as one bounding range per buffer:
    // conservative for barriers, and two tree lookups instead of two per region.
    const bool bounded = copies.size() > 4;
    u64 src_min = ~0ULL, src_max = 0, dst_min = ~0ULL, dst_max = 0;
    if (bounded) {
        for (const auto& copy : copies) {
            src_min = std::min<u64>(src_min, copy.srcOffset);
            src_max = std::max<u64>(src_max, copy.srcOffset + copy.size);
            dst_min = std::min<u64>(dst_min, copy.dstOffset);
            dst_max = std::max<u64>(dst_max, copy.dstOffset + copy.size);
        }
    }

    bool needs_flush{};
    if (bounded) {
        needs_flush = IsBufferAccessed(src, src_min, src_max - src_min) ||
                      IsBufferAccessed(dst, dst_min, dst_max - dst_min, true);
    } else {
        for (const auto& copy : copies) {
            needs_flush |= IsBufferAccessed(src, copy.srcOffset, copy.size);
            needs_flush |= IsBufferAccessed(dst, copy.dstOffset, copy.size, true);
        }
    }
    if (needs_flush) {
        FlushBarriers();
    }

    scheduler.Record([src_handle = src->Handle(), dst_handle = dst->Handle(),
                      regions = scheduler.RecordData(copies)](vk::CommandBuffer cmdbuf) {
        cmdbuf.copyBuffer(src_handle, dst_handle, regions.size(), regions.data());
    });

    if (bounded) {
        AccessBuffer(src, src_min, src_max - src_min, vk::PipelineStageFlagBits2::eCopy,
                     vk::AccessFlagBits2::eTransferRead);
        AccessBuffer(dst, dst_min, dst_max - dst_min, vk::PipelineStageFlagBits2::eCopy,
                     vk::AccessFlagBits2::eTransferWrite);
        return;
    }
    for (const auto& copy : copies) {
        AccessBuffer(src, copy.srcOffset, copy.size, vk::PipelineStageFlagBits2::eCopy,
                     vk::AccessFlagBits2::eTransferRead);
        AccessBuffer(dst, copy.dstOffset, copy.size, vk::PipelineStageFlagBits2::eCopy,
                     vk::AccessFlagBits2::eTransferWrite);
    }
}

void Runtime::FillBuffer(const VideoCore::Buffer* dst, u64 offset, u64 size, u32 value) {
    scheduler.EndRendering();

    if (IsBufferAccessed(dst, offset, size, true)) {
        FlushBarriers();
    }

    scheduler.Record([handle = dst->Handle(), offset, size, value](vk::CommandBuffer cmdbuf) {
        cmdbuf.fillBuffer(handle, offset, size, value);
    });

    AccessBuffer(dst, offset, size, vk::PipelineStageFlagBits2::eClear,
                 vk::AccessFlagBits2::eTransferWrite);
}

void Runtime::CopyFromGuestChunk(u64 chunk_phys, const VideoCore::Buffer* dst,
                                 std::span<const vk::BufferCopy> copies) {
    if (copies.empty()) {
        return;
    }
    const BbGuestMemory::Chunk* chunk = BbGuestMemory::Find(chunk_phys);
    ASSERT_MSG(chunk, "No guest memory chunk at {:#x}", chunk_phys);
    const vk::Buffer src = chunk->buffer;
    scheduler.EndRendering();
    bool needs_flush = false;
    for (const auto& copy : copies) {
        needs_flush |= IsBufferAccessed(dst, copy.dstOffset, copy.size, true);
    }
    if (needs_flush) {
        FlushBarriers();
    }
    scheduler.Record([src, dst_handle = dst->Handle(),
                      regions = scheduler.RecordData(copies)](vk::CommandBuffer cmdbuf) {
        cmdbuf.copyBuffer(src, dst_handle, regions.size(), regions.data());
    });
    for (const auto& copy : copies) {
        AccessBuffer(dst, copy.dstOffset, copy.size, vk::PipelineStageFlagBits2::eCopy,
                     vk::AccessFlagBits2::eTransferWrite);
    }
}

void Runtime::UpdateBuffer(const VideoCore::Buffer* dst, u64 offset, std::span<const u8> data) {
    if (data.empty()) {
        return;
    }
    scheduler.EndRendering();
    if (IsBufferAccessed(dst, offset, data.size(), true)) {
        FlushBarriers();
    }
    constexpr std::size_t Piece = 32 * 1024; // vkCmdUpdateBuffer takes 64 KiB at most
    for (std::size_t at = 0; at < data.size(); at += Piece) {
        const auto piece = data.subspan(at, std::min(Piece, data.size() - at));
        scheduler.Record([handle = dst->Handle(), to = offset + at,
                          bytes = scheduler.RecordData(piece)](vk::CommandBuffer cmdbuf) {
            cmdbuf.updateBuffer(handle, to, bytes.size_bytes(), bytes.data());
        });
    }
    AccessBuffer(dst, offset, data.size(), vk::PipelineStageFlagBits2::eClear,
                 vk::AccessFlagBits2::eTransferWrite);
}

void Runtime::InlineData(VideoCore::Buffer* dst, u64 offset, u32 value) {
    scheduler.EndRendering();

    if (IsBufferAccessed(dst, offset, sizeof(value), true)) {
        FlushBarriers();
    }

    scheduler.Record([handle = dst->Handle(), offset, value](vk::CommandBuffer cmdbuf) {
        cmdbuf.updateBuffer(handle, offset, sizeof(value), &value);
    });

    AccessBuffer(dst, offset, sizeof(value), vk::PipelineStageFlagBits2::eClear,
                 vk::AccessFlagBits2::eTransferWrite);
}

bool Runtime::Transit(VideoCore::Image* image, vk::ImageLayout dst_layout,
                      vk::PipelineStageFlags2 dst_stage, vk::AccessFlags2 dst_access,
                      std::optional<VideoCore::SubresourceRange> subres_range) {
    BeforeImageAccess();
    if (scene_targets) scene_targets->NativeAccess(*image, dst_access);
    const size_t prev_num_barriers = static_cast<size_t>(image_barriers.size());
    image->GetBarriers(image_barriers, dst_layout, dst_access, dst_stage, subres_range);
    return image_barriers.size() != prev_num_barriers;
}

Runtime::TransferMark::TransferMark(Runtime& runtime, const char* what,
                                    const VideoCore::Image& image) {
    profiler = GpuProfiler::Get();
    if (!profiler || !profiler->Records(&runtime.scheduler)) {
        profiler = nullptr;
        return;
    }
    resume = profiler->Current();
    const auto& info = image.info;
    const u64 key = (u64(reinterpret_cast<uintptr_t>(what)) << 20) ^
                    (u64(info.pixel_format) << 40) ^ (u64(info.size.width) << 16) ^
                    info.size.height ^ 0x7A5Full;
    profiler->Mark(key, [&] {
        return fmt::format("{} {} {}x{}", what, vk::to_string(info.pixel_format),
                           info.size.width, info.size.height);
    });
}

Runtime::TransferMark::~TransferMark() {
    if (profiler && resume) {
        profiler->Resume(resume);
    }
}

void Runtime::UploadImage(VideoCore::Image* dst, const VideoCore::Buffer* src,
                          std::span<const vk::BufferImageCopy> upload_copies) {
    SetBackingSamples(dst, dst->info.num_samples, false);
    scheduler.EndRendering();
    const TransferMark mark{*this, "image upload", *dst};

    bool needs_flush =
        Transit(dst, vk::ImageLayout::eTransferDstOptimal, vk::PipelineStageFlagBits2::eCopy,
                vk::AccessFlagBits2::eTransferWrite);
    for (const auto& copy : upload_copies) {
        const auto copy_size = BufferImageCopySize(copy, dst->info.pixel_format);
        needs_flush |= IsBufferAccessed(src, copy.bufferOffset, copy_size);
    }
    if (needs_flush) {
        FlushBarriers();
    }

    scheduler.Record([src_handle = src->Handle(), image = dst->GetImage(),
                      regions = scheduler.RecordData(upload_copies)](vk::CommandBuffer cmdbuf) {
        cmdbuf.copyBufferToImage(src_handle, image, vk::ImageLayout::eTransferDstOptimal,
                                 regions.size(), regions.data());
    });

    for (const auto& copy : upload_copies) {
        const auto copy_size = BufferImageCopySize(copy, dst->info.pixel_format);
        AccessBuffer(src, copy.bufferOffset, copy_size, vk::PipelineStageFlagBits2::eCopy,
                     vk::AccessFlagBits2::eTransferRead);
    }

    dst->flags &= ~VideoCore::ImageFlagBits::Dirty;
}

void Runtime::DownloadImage(VideoCore::Image* src, const VideoCore::Buffer* dst,
                            std::span<const vk::BufferImageCopy> download_copies) {
    SetBackingSamples(src, src->info.num_samples);
    scheduler.EndRendering();
    const TransferMark mark{*this, "image download", *src};

    bool needs_flush =
        Transit(src, vk::ImageLayout::eTransferSrcOptimal, vk::PipelineStageFlagBits2::eCopy,
                vk::AccessFlagBits2::eTransferRead);
    for (const auto& copy : download_copies) {
        const auto copy_size = BufferImageCopySize(copy, src->info.pixel_format);
        needs_flush |= IsBufferAccessed(dst, copy.bufferOffset, copy_size, true);
    }
    if (needs_flush) {
        FlushBarriers();
    }

    scheduler.Record([image = src->GetImage(), buffer = dst->Handle(),
                      regions = scheduler.RecordData(download_copies)](vk::CommandBuffer cmdbuf) {
        cmdbuf.copyImageToBuffer(image, vk::ImageLayout::eTransferSrcOptimal, buffer,
                                 regions.size(), regions.data());
    });

    for (const auto& copy : download_copies) {
        const auto copy_size = BufferImageCopySize(copy, src->info.pixel_format);
        AccessBuffer(dst, copy.bufferOffset, copy_size, vk::PipelineStageFlagBits2::eCopy,
                     vk::AccessFlagBits2::eTransferWrite);
    }
}

void Runtime::CopyImage(VideoCore::Image* src, VideoCore::Image* dst) {
    const u32 num_mips = std::min(src->info.resources.levels, dst->info.resources.levels);

    // Format mismatch warning (safe but useful)
    if (src->info.pixel_format != dst->info.pixel_format) {
        LOG_DEBUG(Render_Vulkan,
                  "Copy between different formats: src={}, dst={}. "
                  "Result may be undefined.",
                  vk::to_string(src->info.pixel_format), vk::to_string(dst->info.pixel_format));
    }

    const u32 base_width = src->info.size.width;
    const u32 base_height = src->info.size.height;
    const u32 base_depth =
        dst->info.type == AmdGpu::ImageType::Color3D ? dst->info.size.depth : src->info.size.depth;

    // Match sample count before copying
    SetBackingSamples(dst, dst->info.num_samples, false);
    SetBackingSamples(src, src->info.num_samples);

    boost::container::small_vector<vk::ImageCopy, 8> regions;

    const vk::ImageAspectFlags src_aspect = src->aspect_mask & ~vk::ImageAspectFlagBits::eStencil;
    const vk::ImageAspectFlags dst_aspect = dst->aspect_mask & ~vk::ImageAspectFlagBits::eStencil;

    const bool src_is_2d = ConvertImageType(src->info.type) == vk::ImageType::e2D;
    const bool src_is_3d = ConvertImageType(src->info.type) == vk::ImageType::e3D;

    const bool dst_is_2d = ConvertImageType(dst->info.type) == vk::ImageType::e2D;
    const bool dst_is_3d = ConvertImageType(dst->info.type) == vk::ImageType::e3D;

    const bool is_2d_to_3d = src_is_2d && dst_is_3d;
    const bool is_3d_to_2d = src_is_3d && dst_is_2d;
    const bool is_same_type = !is_2d_to_3d && !is_3d_to_2d;

    for (u32 mip = 0; mip < num_mips; ++mip) {
        const u32 mip_w = std::max(base_width >> mip, 1u);
        const u32 mip_h = std::max(base_height >> mip, 1u);
        const u32 mip_d = std::max(base_depth >> mip, 1u);

        const auto [src_layers, dst_layers] = SanitizeCopyLayers(src->info, dst->info, mip_d);

        vk::ImageCopy region{};
        region.srcSubresource.aspectMask = src_aspect;
        region.srcSubresource.mipLevel = mip;
        region.srcSubresource.baseArrayLayer = 0;
        region.dstSubresource.aspectMask = dst_aspect;
        region.dstSubresource.mipLevel = mip;
        region.dstSubresource.baseArrayLayer = 0;

        if (is_same_type) {
            // 2D->2D OR 3D->3D
            if (src_is_3d) {
                // 3D images must use layerCount=1
                region.srcSubresource.layerCount = 1;
                region.dstSubresource.layerCount = 1;
                region.extent = vk::Extent3D(mip_w, mip_h, mip_d);
            } else {
                // Array images
                const u32 copy_layers = std::min(src_layers, dst_layers);
                region.srcSubresource.layerCount = copy_layers;
                region.dstSubresource.layerCount = copy_layers;
                region.extent = vk::Extent3D(mip_w, mip_h, 1);
            }
        } else if (is_2d_to_3d) {
            // 2D array -> 3D volume
            region.srcSubresource.layerCount = src_layers;
            region.dstSubresource.layerCount = 1;
            region.extent = vk::Extent3D(mip_w, mip_h, src_layers);
        } else if (is_3d_to_2d) {
            // 3D volume -> 2D array
            region.srcSubresource.layerCount = 1;
            region.dstSubresource.layerCount = dst_layers;
            region.extent = vk::Extent3D(mip_w, mip_h, dst_layers);
        }

        regions.push_back(region);
    }

    scheduler.EndRendering();

    bool needs_flush =
        Transit(src, vk::ImageLayout::eTransferSrcOptimal, vk::PipelineStageFlagBits2::eCopy,
                vk::AccessFlagBits2::eTransferRead);
    needs_flush |= Transit(dst, vk::ImageLayout::eTransferDstOptimal,
                           vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite);
    if (needs_flush) {
        FlushBarriers();
    }

    scheduler.Record([src_image = src->GetImage(), dst_image = dst->GetImage(),
                      regions](vk::CommandBuffer cmdbuf) {
        cmdbuf.copyImage(src_image, vk::ImageLayout::eTransferSrcOptimal, dst_image,
                         vk::ImageLayout::eTransferDstOptimal, regions);
    });

    dst->flags |= (src->flags & VideoCore::ImageFlagBits::GpuModified);
    dst->flags &= ~VideoCore::ImageFlagBits::Dirty;
}

void Runtime::CopyImageWithBuffer(VideoCore::Image* src, VideoCore::Image* dst,
                                  const VideoCore::Buffer* buffer, u64 offset) {
    const u32 num_mips = std::min(src->info.resources.levels, dst->info.resources.levels);
    const u32 num_layers = std::min(src->info.resources.layers, dst->info.resources.layers);
    ASSERT(src->info.resources.layers == dst->info.resources.layers && num_mips == 1);

    SetBackingSamples(dst, dst->info.num_samples, false);
    SetBackingSamples(src, src->info.num_samples);

    vk::BufferImageCopy buffer_copy = {
        .bufferOffset = offset,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource{
            .aspectMask = src->aspect_mask & ~vk::ImageAspectFlagBits::eStencil,
            .mipLevel = 0u,
            .baseArrayLayer = 0,
            .layerCount = num_layers,
        },
        .imageOffset = {0, 0, 0},
        .imageExtent = {src->info.size.width, src->info.size.height, src->info.size.depth},
    };
    const auto copy_size = BufferImageCopySize(buffer_copy, src->info.pixel_format);

    scheduler.EndRendering();

    bool needs_flush =
        Transit(src, vk::ImageLayout::eTransferSrcOptimal, vk::PipelineStageFlagBits2::eCopy,
                vk::AccessFlagBits2::eTransferRead);
    needs_flush |= Transit(dst, vk::ImageLayout::eTransferDstOptimal,
                           vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite);
    needs_flush |= IsBufferAccessed(buffer, offset, copy_size);
    if (needs_flush) {
        FlushBarriers();
    }

    const auto download_copy = buffer_copy;
    buffer_copy.imageSubresource.aspectMask = dst->aspect_mask & ~vk::ImageAspectFlagBits::eStencil;
    scheduler.Record([src_image = src->GetImage(), dst_image = dst->GetImage(),
                      handle = buffer->Handle(), download_copy,
                      upload_copy = buffer_copy](vk::CommandBuffer cmdbuf) {
        cmdbuf.copyImageToBuffer(src_image, vk::ImageLayout::eTransferSrcOptimal, handle,
                                 download_copy);
        const vk::MemoryBarrier2 post_copy_barrier = {
            .srcStageMask = vk::PipelineStageFlagBits2::eCopy,
            .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eCopy,
            .dstAccessMask = vk::AccessFlagBits2::eTransferRead,
        };
        cmdbuf.pipelineBarrier2(vk::DependencyInfo{
            .dependencyFlags = vk::DependencyFlagBits::eByRegion,
            .memoryBarrierCount = 1u,
            .pMemoryBarriers = &post_copy_barrier,
        });
        cmdbuf.copyBufferToImage(handle, dst_image, vk::ImageLayout::eTransferDstOptimal,
                                 upload_copy);
    });

    dst->flags |= (src->flags & VideoCore::ImageFlagBits::GpuModified);
    dst->flags &= ~VideoCore::ImageFlagBits::Dirty;
}

void Runtime::CopyMip(VideoCore::Image* src, VideoCore::Image* dst, u32 mip, u32 slice) {
    const auto dst_dim = dst->info.props.is_block ? 2 : 0;
    const auto mip_block_w = std::max(dst->info.size.width >> (mip + dst_dim), 1u);
    const auto mip_block_h = std::max(dst->info.size.height >> (mip + dst_dim), 1u);
    const auto mip_block_p = std::max(dst->info.mips_layout[mip].pitch >> dst_dim, 1u);

    const auto src_dim = src->info.props.is_block ? 2 : 0;
    ASSERT(mip_block_w == (src->info.size.width >> src_dim));
    ASSERT(mip_block_h == (src->info.size.height >> src_dim));
    ASSERT(mip_block_p == (src->info.pitch >> src_dim));

    const auto [src_layers, dst_layers] =
        SanitizeCopyLayers(src->info, dst->info, src->info.size.depth);

    const vk::ImageCopy image_copy{
        .srcSubresource{
            .aspectMask = src->aspect_mask,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = src_layers,
        },
        .dstSubresource{
            .aspectMask = src->aspect_mask,
            .mipLevel = mip,
            .baseArrayLayer = slice,
            .layerCount = dst_layers,
        },
        .extent = {src->info.size.width, src->info.size.height, src->info.size.depth},
    };

    SetBackingSamples(dst, dst->info.num_samples);
    SetBackingSamples(src, src->info.num_samples);

    scheduler.EndRendering();

    bool needs_flush =
        Transit(src, vk::ImageLayout::eTransferSrcOptimal, vk::PipelineStageFlagBits2::eCopy,
                vk::AccessFlagBits2::eTransferRead);
    needs_flush |= Transit(dst, vk::ImageLayout::eTransferDstOptimal,
                           vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite);
    if (needs_flush) {
        FlushBarriers();
    }

    scheduler.Record([src_image = src->GetImage(), dst_image = dst->GetImage(),
                      image_copy](vk::CommandBuffer cmdbuf) {
        cmdbuf.copyImage(src_image, vk::ImageLayout::eTransferSrcOptimal, dst_image,
                         vk::ImageLayout::eTransferDstOptimal, image_copy);
    });

    dst->flags |= (src->flags & VideoCore::ImageFlagBits::GpuModified);
    dst->flags &= ~VideoCore::ImageFlagBits::Dirty;
}

void Runtime::CopyColorAndDepth(VideoCore::Image* src, VideoCore::Image* dst) {
    if (src->info.num_samples == 1 && dst->info.num_samples == 1) {
        if (instance.IsMaintenance8Supported() ||
            src->info.props.is_depth == dst->info.props.is_depth) {
            CopyImage(src, dst);
        } else {
            // Perform depth from/to color copy using the intermediate copy buffer.
            static constexpr size_t COPY_BUFFER_SIZE = 128_MB;
            const auto copy_ref =
                staging_pool.Request(COPY_BUFFER_SIZE, VideoCore::MemoryType::DeviceLocal);
            CopyImageWithBuffer(src, dst, copy_ref.buffer, copy_ref.offset);
        }
    } else if (src->info.num_samples == 1 && dst->info.num_samples > 1 &&
               dst->info.props.is_depth) {
        // Perform a rendering pass to transfer the channels of source as samples in dest.
        bool needs_flush =
            Transit(src, vk::ImageLayout::eShaderReadOnlyOptimal,
                    vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderRead);
        needs_flush |= Transit(dst, vk::ImageLayout::eDepthStencilAttachmentOptimal,
                               vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                                   vk::PipelineStageFlagBits2::eLateFragmentTests,
                               vk::AccessFlagBits2::eDepthStencilAttachmentWrite);
        if (needs_flush) {
            FlushBarriers();
        }

        blit_helper->ReinterpretColorAsMsDepth(
            dst->info.size.width, dst->info.size.height, dst->info.num_samples,
            src->info.pixel_format, dst->info.pixel_format, src->GetImage(), dst->GetImage());
    } else {
        LOG_WARNING(Render_Vulkan, "Unimplemented depth overlap copy");
    }
}

void Runtime::CopyDepthStencil(VideoCore::Image* src, VideoCore::Image* dst,
                               const VideoCore::SubresourceRange& sub_range) {
    scheduler.EndRendering();

    bool needs_flush =
        Transit(src, vk::ImageLayout::eTransferSrcOptimal, vk::PipelineStageFlagBits2::eCopy,
                vk::AccessFlagBits2::eTransferRead);
    needs_flush |= Transit(dst, vk::ImageLayout::eTransferDstOptimal,
                           vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite);
    if (needs_flush) {
        FlushBarriers();
    }

    const auto aspect_mask = src->aspect_mask & dst->aspect_mask;

    const vk::ImageCopy region = {
        .srcSubresource{
            .aspectMask = aspect_mask,
            .mipLevel = 0,
            .baseArrayLayer = sub_range.base.layer,
            .layerCount = sub_range.extent.layers,
        },
        .srcOffset = {0, 0, 0},
        .dstSubresource{
            .aspectMask = aspect_mask,
            .mipLevel = 0,
            .baseArrayLayer = sub_range.base.layer,
            .layerCount = sub_range.extent.layers,
        },
        .dstOffset = {0, 0, 0},
        .extent = {dst->info.size.width, dst->info.size.height, 1},
    };

    scheduler.Record([src_image = src->GetImage(), dst_image = dst->GetImage(),
                      region](vk::CommandBuffer cmdbuf) {
        cmdbuf.copyImage(src_image, vk::ImageLayout::eTransferSrcOptimal, dst_image,
                         vk::ImageLayout::eTransferDstOptimal, region);
    });

    dst->flags |= VideoCore::ImageFlagBits::GpuModified;
    dst->flags &= ~VideoCore::ImageFlagBits::Dirty;
}

void Runtime::ResolveImage(VideoCore::Image* src, VideoCore::Image* dst,
                           const VideoCore::SubresourceRange& src_range,
                           const VideoCore::SubresourceRange& dst_range) {
    SetBackingSamples(dst, 1, false);
    scheduler.EndRendering();

    const bool needs_resolve = src->backing->num_samples != 1;
    const auto dst_stage =
        needs_resolve ? vk::PipelineStageFlagBits2::eResolve : vk::PipelineStageFlagBits2::eCopy;

    bool needs_flush = Transit(src, vk::ImageLayout::eTransferSrcOptimal, dst_stage,
                               vk::AccessFlagBits2::eTransferRead);
    needs_flush |= Transit(dst, vk::ImageLayout::eTransferDstOptimal, dst_stage,
                           vk::AccessFlagBits2::eTransferWrite);
    if (needs_flush) {
        FlushBarriers();
    }

    const auto [src_layers, dst_layers] = SanitizeCopyLayers(src->info, dst->info, 1);
    if (!needs_resolve) {
        const vk::ImageCopy region = {
            .srcSubresource{
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .mipLevel = 0,
                .baseArrayLayer = src_range.base.layer,
                .layerCount = src_layers,
            },
            .srcOffset = {0, 0, 0},
            .dstSubresource{
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .mipLevel = 0,
                .baseArrayLayer = dst_range.base.layer,
                .layerCount = dst_layers,
            },
            .dstOffset = {0, 0, 0},
            .extent = {src->info.size.width, src->info.size.height, 1},
        };
        scheduler.Record([src_image = src->GetImage(), dst_image = dst->GetImage(),
                          region](vk::CommandBuffer cmdbuf) {
            cmdbuf.copyImage(src_image, vk::ImageLayout::eTransferSrcOptimal, dst_image,
                             vk::ImageLayout::eTransferDstOptimal, region);
        });
    } else {
        const vk::ImageResolve region = {
            .srcSubresource{
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .mipLevel = 0,
                .baseArrayLayer = src_range.base.layer,
                .layerCount = src_layers,
            },
            .srcOffset = {0, 0, 0},
            .dstSubresource{
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .mipLevel = 0,
                .baseArrayLayer = dst_range.base.layer,
                .layerCount = dst_layers,
            },
            .dstOffset = {0, 0, 0},
            .extent = {src->info.size.width, src->info.size.height, 1},
        };
        scheduler.Record([src_image = src->GetImage(), dst_image = dst->GetImage(),
                          region](vk::CommandBuffer cmdbuf) {
            cmdbuf.resolveImage(src_image, vk::ImageLayout::eTransferSrcOptimal, dst_image,
                                vk::ImageLayout::eTransferDstOptimal, region);
        });
    }

    dst->flags |= VideoCore::ImageFlagBits::GpuModified;
    dst->flags &= ~VideoCore::ImageFlagBits::Dirty;
}

void Runtime::ClearImage(VideoCore::Image* dst, const VideoCore::SubresourceRange& range,
                         const vk::ClearValue& clear_value) {
    scheduler.EndRendering();

    const bool needs_flush =
        Transit(dst, vk::ImageLayout::eTransferDstOptimal, vk::PipelineStageFlagBits2::eClear,
                vk::AccessFlagBits2::eTransferWrite, range);
    if (needs_flush) {
        FlushBarriers();
    }

    const vk::ImageSubresourceRange vk_range = {
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .baseMipLevel = range.base.level,
        .levelCount = range.extent.levels,
        .baseArrayLayer = range.base.layer,
        .layerCount = range.extent.layers,
    };
    scheduler.Record([image = dst->GetImage(), color = clear_value.color,
                      vk_range](vk::CommandBuffer cmdbuf) {
        cmdbuf.clearColorImage(image, vk::ImageLayout::eTransferDstOptimal, color, vk_range);
    });

    dst->flags |= VideoCore::ImageFlagBits::GpuModified;
    dst->flags &= ~VideoCore::ImageFlagBits::Dirty;
}

void Runtime::SetBackingSamples(VideoCore::Image* image, u32 num_samples, bool copy_backing) {
    BeforeImageAccess();
    auto& backing = image->backing;
    auto& backing_images = image->backing_images;
    const auto& info = image->info;
    if (!backing || backing->num_samples == num_samples) {
        return;
    }
    ASSERT_MSG(!image->info.props.is_depth, "Swapping samples is only valid for color images");
    VideoCore::Image::BackingImage* new_backing;
    auto it = std::ranges::find_if(backing_images, [num_samples](const auto& candidate) {
        return !candidate.retired && candidate.num_samples == num_samples;
    });
    if (it == backing_images.end()) {
        auto new_image_ci = backing->image.image_ci;
        new_image_ci.samples = LiverpoolToVK::NumSamples(num_samples, image->supported_samples);

        new_backing = &backing_images.emplace_back();
        new_backing->num_samples = num_samples;
        CreateImage(new_backing->image, new_image_ci);

        Vulkan::SetObjectName(instance.GetDevice(), new_backing->image.image,
                              "Image {}x{}x{} {} {} {:#x}:{:#x} L:{} M:{} S:{} (backing)",
                              info.size.width, info.size.height, info.size.depth,
                              AmdGpu::NameOf(info.tile_mode), vk::to_string(info.pixel_format),
                              info.guest_address, info.guest_size, info.resources.layers,
                              info.resources.levels, num_samples);
    } else {
        new_backing = std::addressof(*it);
    }

    if (copy_backing) {
        scheduler.EndRendering();
        ASSERT(image->info.resources.levels == 1 && image->info.resources.layers == 1);

        // Transition current backing to shader read layout
        Transit(image, vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderRead);

        // Transition dest backing to color attachment layout, not caring of previous contents
        constexpr auto dst_stage = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
        constexpr auto dst_access =
            vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite;
        constexpr auto dst_layout = vk::ImageLayout::eColorAttachmentOptimal;
        image_barriers.push_back(vk::ImageMemoryBarrier2{
            .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .srcAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
            .dstStageMask = dst_stage,
            .dstAccessMask = dst_access,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = dst_layout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = new_backing->image,
            .subresourceRange{
                .aspectMask = image->aspect_mask,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = image->info.resources.layers,
            },
        });
        FlushBarriers();

        // Copy between ms and non ms backing images
        blit_helper->CopyBetweenMsImages(
            info.size.width, info.size.height, new_backing->num_samples, info.pixel_format,
            backing->num_samples > 1, backing->image, new_backing->image);

        // Update current layout in tracker to new backings layout
        new_backing->state.layout = dst_layout;
        new_backing->state.access_mask = dst_access;
        new_backing->state.pl_stage = dst_stage;
    }

    backing = new_backing;
}

bool Runtime::IsBufferAccessed(const VideoCore::Buffer* handle, u64 offset, u64 size,
                               bool check_read_access) {
    const AddressRange range = {
        .resource = reinterpret_cast<u64>(handle),
        .range_start = offset,
        .range_end = offset + size - 1,
    };
    if (global_write) {
        return true; // bbport: a paged write may have touched it (AccessGlobal)
    }
    bool has_access = barrier_tracker.FindRange(range, Access::Write);
    if (check_read_access && !has_access) {
        has_access |= barrier_tracker.FindRange(range, Access::Read);
    }
    return has_access;
}

void Runtime::AccessBuffer(const VideoCore::Buffer* handle, u64 offset, u64 size,
                           vk::PipelineStageFlags2 src_stage, vk::AccessFlags2 src_access) {
    AddressRange range = {
        .resource = reinterpret_cast<u64>(handle),
        .range_start = offset,
        .range_end = offset + size - 1,
    };

    constexpr static vk::AccessFlags2 READ_MASK =
        vk::AccessFlagBits2::eIndexRead | vk::AccessFlagBits2::eVertexAttributeRead |
        vk::AccessFlagBits2::eUniformRead | vk::AccessFlagBits2::eShaderRead |
        vk::AccessFlagBits2::eColorAttachmentRead |
        vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eTransferRead |
        vk::AccessFlagBits2::eMemoryRead;

    constexpr static vk::AccessFlags2 WRITE_MASK =
        vk::AccessFlagBits2::eShaderWrite | vk::AccessFlagBits2::eColorAttachmentWrite |
        vk::AccessFlagBits2::eDepthStencilAttachmentWrite | vk::AccessFlagBits2::eTransferWrite |
        vk::AccessFlagBits2::eMemoryWrite | vk::AccessFlagBits2::eTransformFeedbackWriteEXT;

    // bbport: reads are tracked at 4 KiB granularity. Constant data comes from ring allocations
    // at a new offset every draw; rounded, they land in ranges already present and the
    // insert returns early instead of growing the tree until the next barrier flush.
    // Wider read ranges only add barriers, never drop one.
    if (!(src_access & WRITE_MASK) && !BbToggle::Disabled(BbToggle::CoarseReadTracking)) {
        range.range_start &= ~u64{0xFFF};
        range.range_end |= 0xFFF;
    }
    const auto insert = [&](Access access) {
        if (BbToggle::Disabled(BbToggle::AccessMemo)) {
            barrier_tracker.InsertRange(range, access);
            return;
        }
        u64 hash = range.resource ^ range.range_start * 0x9E3779B97F4A7C15ull ^
                   range.range_end << 13 ^ u64(access) << 61;
        hash = (hash ^ hash >> 31) * 0xFF51AFD7ED558CCDull;
        auto& memo = access_memo[(hash ^ hash >> 29) % access_memo.size()];
        if (memo.epoch == access_epoch && memo.resource == range.resource &&
            memo.start == range.range_start && memo.end == range.range_end &&
            memo.access == u32(access)) {
            return;
        }
        barrier_tracker.InsertRange(range, access);
        memo = {range.resource, range.range_start, range.range_end, access_epoch, u32(access)};
    };
    if (src_access & WRITE_MASK) {
        insert(Access::Write);
    }
    if (src_access & READ_MASK) {
        insert(Access::Read);
    }

    memory_barrier.srcStageMask |= src_stage;
    memory_barrier.srcAccessMask |= src_access & WRITE_MASK;
}

void Runtime::AccessGlobal(vk::PipelineStageFlags2 src_stage, vk::AccessFlags2 src_access) {
    memory_barrier.srcStageMask |= src_stage;
    if (src_access & vk::AccessFlagBits2::eShaderWrite) {
        memory_barrier.srcAccessMask |= vk::AccessFlagBits2::eShaderWrite;
        global_write = true;
    }
}

void Runtime::FlushImageBarriers() {
    BeforeImageAccess();
    if (image_barriers.empty()) {
        return;
    }
    scheduler.Record([images = scheduler.RecordData(std::span<const vk::ImageMemoryBarrier2>(
                          image_barriers.data(), image_barriers.size()))](vk::CommandBuffer cmdbuf) {
        cmdbuf.pipelineBarrier2({
            .imageMemoryBarrierCount = static_cast<u32>(images.size()),
            .pImageMemoryBarriers = images.data(),
        });
    });
    image_barriers.clear();
}

void Runtime::FlushBarriers() {
    BeforeImageAccess();
    vk::DependencyInfo dep_info{};

    if (memory_barrier.srcStageMask) {
        dep_info.pMemoryBarriers = &memory_barrier;
        dep_info.memoryBarrierCount = 1U;
    }
    if (!image_barriers.empty()) {
        dep_info.pImageMemoryBarriers = image_barriers.data();
        dep_info.imageMemoryBarrierCount = static_cast<u32>(image_barriers.size());
    }

    if (!dep_info.memoryBarrierCount && !dep_info.imageMemoryBarrierCount) {
        return;
    }

    scheduler.EndRendering();
    scheduler.Record([memory = memory_barrier, has_memory = dep_info.memoryBarrierCount != 0,
                      images = scheduler.RecordData(std::span<const vk::ImageMemoryBarrier2>(
                          image_barriers.data(), image_barriers.size()))](vk::CommandBuffer cmdbuf) {
        const vk::DependencyInfo info = {
            .memoryBarrierCount = has_memory ? 1U : 0U,
            .pMemoryBarriers = has_memory ? &memory : nullptr,
            .imageMemoryBarrierCount = static_cast<u32>(images.size()),
            .pImageMemoryBarriers = images.data(),
        };
        cmdbuf.pipelineBarrier2(info);
    });

    memory_barrier.srcStageMask = vk::PipelineStageFlagBits2::eNone;
    memory_barrier.srcAccessMask = vk::AccessFlagBits2::eNone;

    image_barriers.clear();
    barrier_tracker.Clear();
    global_write = false;
    ++access_epoch;
}

[[nodiscard]] static VkMemoryPropertyFlags MemoryUsagePreferredVmaFlags(
    VideoCore::MemoryType type) {
    return type != VideoCore::MemoryType::DeviceLocal ? VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                                                      : VkMemoryPropertyFlagBits{};
}

[[nodiscard]] static VmaAllocationCreateFlags MemoryUsageVmaFlags(VideoCore::MemoryType type) {
    switch (type) {
    case VideoCore::MemoryType::HostUncached:
    case VideoCore::MemoryType::Stream:
        return VMA_ALLOCATION_CREATE_MAPPED_BIT |
               VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    case VideoCore::MemoryType::HostCached:
        return VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
    case VideoCore::MemoryType::DeviceLocal:
    default:
        return {};
    }
}

[[nodiscard]] static VmaMemoryUsage MemoryUsageVma(VideoCore::MemoryType type) {
    switch (type) {
    case VideoCore::MemoryType::DeviceLocal:
    case VideoCore::MemoryType::Stream:
        return VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    case VideoCore::MemoryType::HostUncached:
    case VideoCore::MemoryType::HostCached:
        return VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    default:
        return VMA_MEMORY_USAGE_UNKNOWN;
    }
}

VideoCore::BufferMemory Runtime::CreateBuffer(VideoCore::UniqueBuffer& out,
                                              const vk::BufferCreateInfo& info,
                                              VideoCore::MemoryType mem_type) {
    using VideoCore::MemoryType;
    const vk::Device device = instance.GetDevice();
    VmaAllocator allocator = instance.GetAllocator();
    vk::BufferCreateInfo buffer_ci = info;
    // bbport: arenas are also read by the readback queue (BufferCache::DownloadMemory).
    const std::array<u32, 2> families = {instance.GetGraphicsQueueFamilyIndex(),
                                         instance.GetReadbackQueueFamilyIndex()};
    if (mem_type == MemoryType::Sparse && instance.GetReadbackQueue()) {
        buffer_ci.sharingMode = vk::SharingMode::eConcurrent;
        buffer_ci.queueFamilyIndexCount = static_cast<u32>(families.size());
        buffer_ci.pQueueFamilyIndices = families.data();
    }
    out.owner = this;
    VmaAllocationInfo alloc_info{};

    const bool with_bda = bool(buffer_ci.usage & vk::BufferUsageFlagBits::eShaderDeviceAddress);
    if (mem_type != MemoryType::Sparse) {
        const VmaAllocationCreateFlags bda_flag =
            with_bda ? VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT : 0;
        const VmaAllocationCreateInfo alloc_ci = {
            .flags =
                VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT | bda_flag | MemoryUsageVmaFlags(mem_type),
            .usage = MemoryUsageVma(mem_type),
            .requiredFlags = 0,
            .preferredFlags = MemoryUsagePreferredVmaFlags(mem_type),
            .pool = VK_NULL_HANDLE,
            .pUserData = nullptr,
        };

        const VkBufferCreateInfo buffer_ci_unsafe = static_cast<VkBufferCreateInfo>(buffer_ci);
        VkBuffer unsafe_buffer{};
        VkResult result = vmaCreateBuffer(allocator, &buffer_ci_unsafe, &alloc_ci, &unsafe_buffer,
                                          &out.allocation, &alloc_info);
        ASSERT_MSG(result == VK_SUCCESS, "Failed allocating buffer with error {}",
                   vk::to_string(vk::Result{result}));
        out.buffer = vk::Buffer{unsafe_buffer};
    } else {
        buffer_ci.flags |=
            vk::BufferCreateFlagBits::eSparseBinding | vk::BufferCreateFlagBits::eSparseResidency;
        // Bound to exported guest memory chunks (BB_GUEST_IN_PLACE) as well as VRAM blocks.
        const vk::ExternalMemoryBufferCreateInfo external{
            .handleTypes = BbGuestMemory::HandleType(),
        };
        if (VideoCore::GuestInPlace()) {
            buffer_ci.pNext = &external;
            if (const auto [result, created] = device.createBuffer(buffer_ci);
                result == vk::Result::eSuccess) {
                out.buffer = created;
                // bbport: VRAM blocks are bound into it too: the external memory type must not
                // keep device-local memory out (a driver may restrict it).
                const auto reqs = device.getBufferMemoryRequirements(created);
                const VkPhysicalDeviceMemoryProperties* props = nullptr;
                vmaGetMemoryProperties(allocator, &props);
                bool vram = false;
                for (u32 i = 0; i < props->memoryTypeCount; ++i) {
                    vram |= (reqs.memoryTypeBits & (1u << i)) &&
                            (props->memoryTypes[i].propertyFlags &
                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
                }
                if (!vram) {
                    std::printf("Guest memory: the arena for guest memory cannot take VRAM; the GPU "
                                "uses copies in VRAM as before\n");
                    device.destroyBuffer(created);
                    out.buffer = vk::Buffer{};
                    VideoCore::DisableGuestInPlace();
                }
            } else {
                std::printf("Guest memory: the arena cannot take exported memory (%s); the GPU "
                            "uses copies in VRAM as before\n",
                            vk::to_string(result).c_str());
                VideoCore::DisableGuestInPlace();
            }
            buffer_ci.pNext = nullptr;
        }
        if (!out.buffer) {
            out.buffer = Vulkan::Check(device.createBuffer(buffer_ci));
        }
    }

    if (with_bda) {
        const vk::BufferDeviceAddressInfo bda_info = {
            .buffer = out.buffer,
        };
        auto bda_result = device.getBufferAddress(bda_info);
        ASSERT_MSG(bda_result != 0, "Failed to get buffer device address (size {:#x}, memory type {})",
                   buffer_ci.size, static_cast<int>(mem_type));
        out.bda_addr = bda_result;
    }

    VideoCore::BufferMemory memory{};
    if (mem_type != MemoryType::Sparse) {
        VkMemoryPropertyFlags property_flags{};
        vmaGetAllocationMemoryProperties(allocator, out.allocation, &property_flags);
        memory.mapped = static_cast<u8*>(alloc_info.pMappedData);
        memory.coherent = property_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    }
    return memory;
}

VideoCore::BufferMemory Runtime::ImportBuffer(VideoCore::UniqueBuffer& out, u64 size,
                                              u64 chunk_phys) {
    const BbGuestMemory::Chunk* chunk = BbGuestMemory::Find(chunk_phys);
    ASSERT_MSG(chunk, "No guest memory chunk at {:#x}", chunk_phys);
    const vk::DeviceMemory memory = chunk->memory;
    const vk::ExternalMemoryBufferCreateInfo external{
        .handleTypes = BbGuestMemory::HandleType(),
    };
    // bbport BB_LAYER_MEMORY: what the GPU binds for the game's memory in place (no arena):
    // vertex, index and indirect data and device addresses too (the memory has them).
    const bool layer = BbGuestMemory::LayerMemory();
    vk::BufferUsageFlags usage =
        vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst |
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eUniformBuffer |
        vk::BufferUsageFlagBits::eUniformTexelBuffer | vk::BufferUsageFlagBits::eStorageTexelBuffer;
    if (layer) {
        usage |= vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eIndexBuffer |
                 vk::BufferUsageFlagBits::eIndirectBuffer |
                 vk::BufferUsageFlagBits::eShaderDeviceAddress;
    }
    const vk::BufferCreateInfo buffer_ci = {
        .pNext = &external,
        .size = size,
        .usage = usage,
        .sharingMode = vk::SharingMode::eExclusive,
    };
    const auto device = instance.GetDevice();
    out.owner = this;
    out.buffer = Vulkan::Check(device.createBuffer(buffer_ci));
    const auto result = device.bindBufferMemory(out.buffer, memory, 0);
    ASSERT_MSG(result == vk::Result::eSuccess, "Binding a guest memory chunk failed: {}",
               vk::to_string(result));
    if (layer) {
        out.bda_addr = device.getBufferAddress({.buffer = out.buffer});
    }
    return {.coherent = true};
}

void Runtime::DestroyBuffer(VideoCore::UniqueBuffer& buffer) {
    if (buffer.allocation) {
        vmaDestroyBuffer(instance.GetAllocator(), buffer.buffer, buffer.allocation);
    } else if (buffer.buffer) {
        instance.GetDevice().destroyBuffer(buffer.buffer);
    }
    buffer.buffer = VK_NULL_HANDLE;
    buffer.allocation = VK_NULL_HANDLE;
    buffer.bda_addr = 0;
}

void Runtime::FlushBuffer(const VideoCore::UniqueBuffer& buffer, u64 offset, u64 size) {
    vmaFlushAllocation(instance.GetAllocator(), buffer.allocation, offset, size);
}

void Runtime::InvalidateBuffer(const VideoCore::UniqueBuffer& buffer, u64 offset, u64 size) {
    vmaInvalidateAllocation(instance.GetAllocator(), buffer.allocation, offset, size);
}

void CreateVmaImage(VideoCore::UniqueImage& image, const vk::ImageCreateInfo& image_ci) {
    image.image_ci = image_ci;
    ASSERT(!image.image);
    // bbport: images of BB_VMA_DEDICATED_MB (4) or more get memory of their own, given back to
    // the driver when they are freed. In shared VMA blocks the texture collector's evictions left
    // holes (~0.9 GB on an RX 7800 XT) that a block only returns once wholly empty.
    static const vk::DeviceSize dedicated_from = [] {
        const char* env = std::getenv("BB_VMA_DEDICATED_MB");
        return vk::DeviceSize(env ? std::strtoull(env, nullptr, 10) : 4) << 20;
    }();
    VmaAllocationCreateFlags dedicated = 0;
    if (image.device && dedicated_from != 0) {
        const vk::DeviceImageMemoryRequirements query{.pCreateInfo = &image.image_ci};
        if (image.device.getImageMemoryRequirements(query).memoryRequirements.size >=
            dedicated_from) {
            dedicated = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
        }
    }
    const VmaAllocationCreateInfo alloc_ci = {
        .flags = VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT | dedicated,
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        .requiredFlags = 0,
        .preferredFlags = 0,
        .pool = VK_NULL_HANDLE,
        .pUserData = nullptr,
    };

    const VkImageCreateInfo image_ci_unsafe = static_cast<VkImageCreateInfo>(image.image_ci);
    VkImage unsafe_image{};
    VmaAllocationInfo alloc_info{};
    VkResult result = vmaCreateImage(image.allocator, &image_ci_unsafe, &alloc_ci, &unsafe_image,
                                     &image.allocation, &alloc_info);
    ASSERT_MSG(result == VK_SUCCESS, "Failed allocating image with error {}",
               vk::to_string(vk::Result{result}));
    image.image = vk::Image{unsafe_image};
    image.size_bytes = alloc_info.size;
    BbStats::vk_image_bytes.fetch_add(image.size_bytes, std::memory_order_relaxed);
}

void DestroyVmaImage(VideoCore::UniqueImage& image) {
    if (image.image) {
        BbStats::vk_image_bytes.fetch_sub(image.size_bytes, std::memory_order_relaxed);
        vmaDestroyImage(image.allocator, image.image, image.allocation);
        image.image = vk::Image{};
        image.allocation = {};
    }
}

void Runtime::CreateImage(VideoCore::UniqueImage& out, const vk::ImageCreateInfo& info) {
    out.owner = this;
    out.device = instance.GetDevice();
    out.allocator = instance.GetAllocator();
    CreateVmaImage(out, info);
}

void Runtime::DestroyImage(VideoCore::UniqueImage& image) {
    DestroyVmaImage(image);
}

vk::SampleCountFlags Runtime::ImageSampleCounts(const vk::ImageCreateInfo& info) const {
    const vk::PhysicalDeviceImageFormatInfo2 format_info{
        .format = info.format,
        .type = info.imageType,
        .tiling = info.tiling,
        .usage = info.usage,
        .flags = info.flags,
    };
    const auto image_format_properties =
        instance.GetPhysicalDevice().getImageFormatProperties2(format_info);
    if (image_format_properties.result == vk::Result::eErrorFormatNotSupported) {
        LOG_ERROR(Render_Vulkan, "image format {} type {} is not supported (flags {}, usage {})",
                  vk::to_string(info.format), vk::to_string(info.imageType),
                  vk::to_string(info.flags), vk::to_string(info.usage));
    }
    return image_format_properties.result == vk::Result::eSuccess
               ? image_format_properties.value.imageFormatProperties.sampleCounts
               : vk::SampleCountFlagBits::e1;
}

void Runtime::CreateImageView(VideoCore::ImageView& out, const vk::ImageViewCreateInfo& info) {
    auto [view_result, view] = instance.GetDevice().createImageViewUnique(info);
    ASSERT_MSG(view_result == vk::Result::eSuccess, "Failed to create image view: {}",
               vk::to_string(view_result));
    out.owner = this;
    out.image_view = std::move(view);
}

void Runtime::DestroyImageView(VideoCore::ImageView& view) {
    view.image_view.reset();
}

void Runtime::CreateSampler(VideoCore::Sampler& out, const vk::SamplerCreateInfo& info) {
    auto [sampler_result, smplr] = instance.GetDevice().createSamplerUnique(info);
    ASSERT_MSG(sampler_result == vk::Result::eSuccess, "Failed to create sampler: {}",
               vk::to_string(sampler_result));
    out.owner = this;
    out.handle = std::move(smplr);
}

void Runtime::DestroySampler(VideoCore::Sampler& sampler) {
    sampler.handle.reset();
}

void Runtime::SetBufferName(const VideoCore::UniqueBuffer& buffer, std::string_view name) {
    Vulkan::SetObjectName(instance.GetDevice(), buffer.buffer, name);
}

void Runtime::SetImageName(const VideoCore::UniqueImage& image, std::string_view name) {
    Vulkan::SetObjectName(instance.GetDevice(), image.image, name);
}

void Runtime::SetImageViewName(const VideoCore::ImageView& view, std::string_view name) {
    Vulkan::SetObjectName(instance.GetDevice(), *view.image_view, name);
}

void Runtime::DispatchTiling(const VideoCore::TilingDispatch& dispatch) {
    tile_kernels->Dispatch(dispatch);
}

void Runtime::CreateFaultParser(u32 page_bits, u32 max_faults) {
    fault_parser = std::make_unique<FaultParser>(instance, scheduler, page_bits, max_faults);
}

void Runtime::ParseFaultBuffer(const VideoCore::FaultParseDispatch& dispatch) {
    fault_parser->Dispatch(dispatch);
}

bool Runtime::GuestMemoryUsable() {
    return BbGuestMemory::PcModelGpu(instance) && BbGuestMemory::Usable(instance);
}

VideoCore::SparseArena* Runtime::GetSparseArena() {
    return sparse_arena.get();
}

VideoCore::FastReadback* Runtime::GetFastReadback() {
    // One readback queue user for the old model's downloads and the module's read traps.
    std::call_once(fast_readback_once, [this] {
        fast_readback = std::make_unique<FastReadback>(instance, scheduler);
    });
    return fast_readback.get();
}

} // namespace Vulkan
