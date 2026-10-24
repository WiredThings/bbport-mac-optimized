// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <mutex>

#include "common/types.h"
#include "video_core/buffer_cache/buffer.h"
#include "video_core/cache_backend.h"
#include "video_core/renderer_vulkan/vk_barrier_tracker.h"
#include "video_core/renderer_vulkan/vk_staging_buffer_pool.h"
#include "video_core/texture_cache/image.h"
#include "video_core/texture_cache/image_view.h"
#include "video_core/texture_cache/sampler.h"
#include "video_core/texture_cache/types.h"

namespace Vulkan {

class BlitHelper;
class FaultParser;
class GpuProfiler;
class SceneTargets;
class SparseArena;
class FastReadback;
class TileKernels;

class Instance;
class Scheduler;

/// The Vulkan implementation of an image with its own VMA allocation: Runtime::CreateImage and
/// the Vulkan host passes (UniqueImage::Create without a CacheBackend) share it. `image.device`
/// and `image.allocator` have to be set.
void CreateVmaImage(VideoCore::UniqueImage& image, const vk::ImageCreateInfo& image_ci);
void DestroyVmaImage(VideoCore::UniqueImage& image);

class Runtime final : public VideoCore::CacheBackend {
public:
    explicit Runtime(const Instance& instance, Scheduler& scheduler);
    ~Runtime() override;

    Scheduler& GetScheduler() {
        return scheduler;
    }

    const Instance& GetInstance() const {
        return instance;
    }

    StagingBufferPool& GetStagingPool() {
        return staging_pool;
    }

    void TickFrame() override;
    SceneTargets* scene_targets = nullptr;

    void CopyBuffer(const VideoCore::Buffer* src, const VideoCore::Buffer* dst,
                    std::span<const vk::BufferCopy> copies) override;

    void FillBuffer(const VideoCore::Buffer* dst, u64 offset, u64 size, u32 value) override;

    void InlineData(VideoCore::Buffer* dst, u64 offset, u32 value) override;
    /// bbport BB_GUEST_IN_PLACE: copies from the game's memory itself (the guest memory chunk
    /// that starts at physical address `chunk_phys`, host memory the CPU writes) into `dst`:
    /// uploads without a CPU copy.
    void CopyFromGuestChunk(u64 chunk_phys, const VideoCore::Buffer* dst,
                            std::span<const vk::BufferCopy> copies) override;
    /// bbport: `data` written into `dst` by the GPU in stream order (vkCmdUpdateBuffer pieces).
    void UpdateBuffer(const VideoCore::Buffer* dst, u64 offset, std::span<const u8> data) override;

    bool Transit(VideoCore::Image* image, vk::ImageLayout dst_layout,
                 vk::PipelineStageFlags2 dst_stage, vk::AccessFlags2 dst_access,
                 std::optional<VideoCore::SubresourceRange> subres_range = {}) override;

    /// BB_GPU_PROFILE: a profiler segment for a transfer of `image` while it lives; the
    /// segment it interrupted continues afterwards.
    class TransferMark {
    public:
        TransferMark(Runtime& runtime, const char* what, const VideoCore::Image& image);
        ~TransferMark();
        TransferMark(const TransferMark&) = delete;
        TransferMark& operator=(const TransferMark&) = delete;

    private:
        GpuProfiler* profiler = nullptr;
        u64 resume = 0;
    };
    void UploadImage(VideoCore::Image* dst, const VideoCore::Buffer* src,
                     std::span<const vk::BufferImageCopy> upload_copies) override;
    void DownloadImage(VideoCore::Image* src, const VideoCore::Buffer* dst,
                       std::span<const vk::BufferImageCopy> download_copies) override;

    void CopyImage(VideoCore::Image* src, VideoCore::Image* dst) override;
    void CopyImageWithBuffer(VideoCore::Image* src, VideoCore::Image* dst,
                             const VideoCore::Buffer* buffer, u64 offset) override;
    void CopyMip(VideoCore::Image* src, VideoCore::Image* dst, u32 mip, u32 slice) override;

    void CopyColorAndDepth(VideoCore::Image* src, VideoCore::Image* dst) override;

    void CopyDepthStencil(VideoCore::Image* src, VideoCore::Image* dst,
                          const VideoCore::SubresourceRange& sub_range) override;

    void ResolveImage(VideoCore::Image* src, VideoCore::Image* dst,
                      const VideoCore::SubresourceRange& src_range,
                      const VideoCore::SubresourceRange& dst_range) override;
    void ClearImage(VideoCore::Image* dst, const VideoCore::SubresourceRange& range,
                    const vk::ClearValue& clear_value) override;

    void SetBackingSamples(VideoCore::Image* image, u32 num_samples,
                           bool copy_backing = true) override;

    void AccessBuffer(const VideoCore::Buffer* handle, u64 offset, u64 size,
                      vk::PipelineStageFlags2 src_stage, vk::AccessFlags2 src_access) override;

    bool IsBufferAccessed(const VideoCore::Buffer* handle, u64 offset, u64 size,
                          bool check_read_access = false) override;

    /// bbport BB_LAYER_MEMORY: a shader reached memory through the page table (any buffer of the
    /// game's memory). A write there makes every later access wait for a barrier.
    void AccessGlobal(vk::PipelineStageFlags2 src_stage, vk::AccessFlags2 src_access) override;

    void FlushBarriers() override;
    /// bbport BB_HOIST_DISPATCH: records the pending image barriers only (a hoisted dispatch's
    /// layout changes, before the held render pass), without ending the render pass.
    void FlushImageBarriers() override;
    [[nodiscard]] bool HasPendingImageBarriers() const noexcept override {
        return !image_barriers.empty();
    }

    /// bbport: runs before this thread changes image state (layouts, pending image
    /// barriers): the rasterizer joins its texture binding helper there (BindHelper).
    void SetImageAccessHook(void (*hook)(void*), void* context) {
        image_access_hook = hook;
        image_access_context = context;
    }
    void BeforeImageAccess() override {
        if (image_access_hook) {
            image_access_hook(image_access_context);
        }
    }

    // The scheduler, staging pool and instance, as the caches use them (VideoCore::CacheBackend).
    VideoCore::StagingSpan StagingRequest(u64 size, VideoCore::MemoryType type, u64 alignment = 0,
                                          bool deferred = false) override;
    void StagingFreeDeferred(const VideoCore::StagingSpan& span) override;
    u64 CurrentTick() const noexcept override;
    bool IsFree(u64 tick) noexcept override;
    bool IsFreeKnown(u64 tick) const noexcept override;
    void RefreshTicks() override;
    void Wait(u64 tick) override;
    void Finish() override;
    void DeferOperation(Common::UniqueFunction<void>&& func) override;
    void DeferPriorityOperation(Common::UniqueFunction<void>&& func) override;
    void PopPendingOperations() override;
    void EndRendering() override;
    bool IsRendering() const noexcept override;
    bool IsRecordingDeferred() const noexcept override;
    void NoteHostCopySource(u64 address, u64 size) override;
    void RecordHostCopy(const BbCopy::Item& item) override;
    vk::Format SupportedFormat(vk::Format format, vk::FormatFeatureFlags2 features) const override;
    u64 DeviceMemoryUsage() const override;
    u64 DeviceMemoryBudgetNow() const override;

    // The caches' own GPU work (VideoCore::CacheBackend): SPIR-V compute kernels.
    void DispatchTiling(const VideoCore::TilingDispatch& dispatch) override;
    void CreateFaultParser(u32 page_bits, u32 max_faults) override;
    void ParseFaultBuffer(const VideoCore::FaultParseDispatch& dispatch) override;

    // Objects (VideoCore::CacheBackend): VMA buffers and images, unique views and samplers.
    VideoCore::BufferMemory CreateBuffer(VideoCore::UniqueBuffer& out,
                                         const vk::BufferCreateInfo& info,
                                         VideoCore::MemoryType type) override;
    VideoCore::BufferMemory ImportBuffer(VideoCore::UniqueBuffer& out, u64 size,
                                         u64 chunk_phys) override;
    void DestroyBuffer(VideoCore::UniqueBuffer& buffer) override;
    void FlushBuffer(const VideoCore::UniqueBuffer& buffer, u64 offset, u64 size) override;
    void InvalidateBuffer(const VideoCore::UniqueBuffer& buffer, u64 offset, u64 size) override;
    void CreateImage(VideoCore::UniqueImage& out, const vk::ImageCreateInfo& info) override;
    void DestroyImage(VideoCore::UniqueImage& image) override;
    vk::SampleCountFlags ImageSampleCounts(const vk::ImageCreateInfo& info) const override;
    void CreateImageView(VideoCore::ImageView& out,
                         const vk::ImageViewCreateInfo& info) override;
    void DestroyImageView(VideoCore::ImageView& view) override;
    void CreateSampler(VideoCore::Sampler& out, const vk::SamplerCreateInfo& info) override;
    void DestroySampler(VideoCore::Sampler& sampler) override;
    void SetBufferName(const VideoCore::UniqueBuffer& buffer, std::string_view name) override;
    void SetImageName(const VideoCore::UniqueImage& image, std::string_view name) override;
    void SetImageViewName(const VideoCore::ImageView& view, std::string_view name) override;

    // Optional memory paths (VideoCore::CacheBackend): the sparse arena and the readback queue.
    bool GuestMemoryUsable() override;
    VideoCore::SparseArena* GetSparseArena() override;
    VideoCore::FastReadback* GetFastReadback() override;

private:
    void (*image_access_hook)(void*) = nullptr;
    void* image_access_context = nullptr;
    const Instance& instance;
    Scheduler& scheduler;
    std::unique_ptr<BlitHelper> blit_helper;
    std::unique_ptr<TileKernels> tile_kernels;
    std::unique_ptr<FaultParser> fault_parser;
    std::unique_ptr<SparseArena> sparse_arena;
    std::unique_ptr<FastReadback> fast_readback; ///< made at first use
    std::once_flag fast_readback_once;
    StagingBufferPool staging_pool;
    BarrierTracker barrier_tracker;
    VideoCore::Image::Barriers image_barriers;
    vk::MemoryBarrier2 memory_barrier{};
    bool global_write = false; ///< bbport: AccessGlobal wrote since the last FlushBarriers
    // bbport: ranges inserted into barrier_tracker since its last Clear(); re-inserting a
    // contained range is a no-op, and draws re-bind the same ranges constantly.
    struct AccessMemo {
        u64 resource;
        u64 start;
        u64 end;
        u32 epoch;
        u32 access;
    };
    std::array<AccessMemo, 512> access_memo{};
    u32 access_epoch = 1;
};

} // namespace Vulkan
