// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <optional>

#include "video_core/scene_passes.h"
#include "video_core/scene_targets_core.h"

namespace VideoCore {
class TextureCache;
}

namespace Metal {

class Device;
class Runtime;
class Scheduler;

/// bbport: the reduced scene targets of the live-resolution path on Metal (plan P7.5 L2,
/// docs/metal/live_resolution_design.md). Counterpart: Vulkan::SceneTargets
/// (vk_scene_resolution.cpp); the proxy bookkeeping, eligibility and coherence steps are the
/// shared VideoCore::SceneTargetsCore's, this is its Metal part: the proxy textures, the copies
/// between a proxy and the guest-size ("native") image, the views.
///
/// Proxies are textures made by Runtime::CreateImage in the native image's (host) pixel format at
/// the proxy size, one level, sampled + render target (never storage), private storage; they are
/// in the residency set like every cache texture and go through Runtime::DestroyImage, which
/// releases them (and their place in the set) after the command buffer of the open tick has
/// completed (D6). Views of a proxy (format, swizzle) are kept with it and released the same way.
///
/// Copies cannot be blits (Metal blits do not scale): a render pass of a fullscreen triangle whose
/// fragment shader reads the source texel at the scaled coordinate (nearest, `read`), one shader
/// per format class: float (also unorm, snorm, sRGB, packed float), uint, sint, depth, and depth
/// with stencil (the stencil exported with [[stencil]] from a stencil view of the source).
/// Pipelines are made on first use by (pixel format, class).
///
/// Proxy-eligible formats (ProxyFormatSupported; mip chains of half resolution targets, which get
/// one proxy per level, take the same colour formats): 2D, single sample textures of R8, RG8, RGBA8,
/// BGRA8 (unorm, snorm, sRGB), R16, RG16, RGBA16 (unorm, snorm, float), R32, RG32, RGBA32 (float),
/// R11G11B10 float, RGB10A2 and BGR10A2 unorm, the uint and sint formats of 8, 16 and 32 bits
/// (1, 2 and 4 channels, RGB10A2 uint), and the depth formats Depth16Unorm, Depth32Float and
/// Depth32Float_Stencil8 (single level only). Not: RGB9E5, 16-bit packed formats, block
/// compressed formats, stencil-only, 3D and array textures, multisampled.
///
/// Threads: the recording thread's, as the Scheduler's (SceneTargetsCore is not thread-safe
/// either); Eligible and the other const queries are pure.
class SceneTargets final : public VideoCore::SceneTargetsCore {
public:
    SceneTargets(Device&, Scheduler&, Runtime&, VideoCore::TextureCache&);
    /// With the image lookup of the caller (the unit test).
    SceneTargets(Device&, Scheduler&, Runtime&, Lookup);
    /// Finishes the GPU work; the proxies go through the runtime's deferred destroys.
    ~SceneTargets() override;

    /// A texture (an id<MTLTexture> as VideoCore::NativeView) standing in for a view of the
    /// image and the layout the caller's bookkeeping holds it in (the ones Vulkan would use).
    struct Target {
        VideoCore::NativeView view{};
        vk::ImageLayout layout{};
    };
    /// The proxy of an image the pass draws into (created and filled when missing); the view is
    /// the whole texture or a view in the attachment's format.
    Target Attachment(VideoCore::ImageId, const VideoCore::ImageViewInfo&, bool clear_color = false);
    /// The proxy of a sampled image when it holds the current content (else nullopt: the native
    /// image is current); a view in the view's format and swizzle.
    std::optional<Target> SampleProxy(const VideoCore::Image&, const VideoCore::ImageViewInfo&);
    /// The proxy of `id` (level 0) for the upscaler to read, filled when it is not valid; the
    /// view is the whole texture (`info.format` selects the pixel format when it differs).
    Target Read(VideoCore::ImageId, const VideoCore::ImageViewInfo&);
    /// Runtime::Transit: a use of the native image. Writes are the access bits of `access`.
    void NativeAccess(VideoCore::Image& image, vk::AccessFlags2 access, bool clear = false);

    /// Called only after validating a complete non-aliasing native CopyImage destination.
    void FullCopyOverwrite(VideoCore::Image& image, const void* source, const void* target);
    /// Resolve a dirty source proxy straight into a complete, disjoint native copy target.
    bool CopyDirectToNative(VideoCore::Image& source, VideoCore::Image& target);
    [[nodiscard]] u64 DirectCopyCount() const;
    /// Existing current RGBA8 or opt-in RGBA16F proxy with exactly 2x native extent; no fill/allocation/coherence change.
    const void* ExistingTilingProxy(const VideoCore::Image&) const;

    [[nodiscard]] u64 DiscardedCopyResolves() const;
    /// Full single-level color clear of an existing proxy; lazy native resolve preserves readers.
    bool ClearProxy(VideoCore::Image&, const VideoCore::SubresourceRange&, const vk::ClearValue&);
    [[nodiscard]] u64 ProxyClearCount() const;
    /// Normal MetalFX RGB + original alpha: fuse the dirty-proxy resolve into the existing merge.
    /// No allocations; false leaves coherence untouched so callers can use their original path.
    bool MergeUpscaleAlpha(VideoCore::Image&, const void* upscaled, const void* alpha_source);
    [[nodiscard]] u64 UpscaleAlphaCount() const;
    /// Descriptor memos must not retain views of a backing replaced on first format need.
    [[nodiscard]] u64 Generation() const noexcept {
        return SceneTargetsCore::Generation() + view_generation;
    }

    /// Host-only nearest copy/scale. Caller owns coherence and ShaderRead/PixelFormatView usage.
    /// Both textures must be single-sample 2D, level zero, with matching format classes.
    void CopyTextures(const void* source, const void* target, const char* label = "Host texture copy");

private:
    struct Proxy;
    struct Impl;

    static Proxy& Of(Entry&);
    bool ProxyFormatSupported(const VideoCore::Image&) const override;
    bool MipChainFormatSupported(const VideoCore::Image&) const override;
    std::unique_ptr<ProxyData> CreateProxy(const VideoCore::Image&,
                                           VideoCore::SceneResolution::Size) override;
    void CopyProxy(Entry&, VideoCore::Image&, bool to_native) override;
    vk::ImageLayout Prepare(Entry&, const VideoCore::Image&, Use) override;
    void Finish() override;
    /// The proxy's texture or a view of it in the pixel format of `info.format` with its swizzle
    /// (an id<MTLTexture> as NativeView).
    VideoCore::NativeView View(Entry&, const VideoCore::Image&, const VideoCore::ImageViewInfo&,
                               bool attachment);

    Device& device;
    Scheduler& scheduler;
    Runtime& runtime;
    std::unique_ptr<Impl> impl;
    u64 view_generation = 0;
};

/// The scene targets behind the core's interface (as Vulkan::SceneTargetsAdapter): the views the
/// targets return are the opaque NativeView values the rasterizer binds and attaches.
class SceneTargetsAdapter final : public VideoCore::ScenePasses {
public:
    explicit SceneTargetsAdapter(SceneTargets& targets_) : targets{targets_} {}

    bool Tracks(u64 image_uid) const override {
        return targets.Tracks(image_uid);
    }
    bool Eligible(const VideoCore::Image& image) const override {
        return targets.Eligible(image);
    }
    bool EligibleScene(const VideoCore::Image& image) const override {
        return targets.EligibleScene(image);
    }
    VideoCore::SceneResolution::Size ProxySize(const VideoCore::Image& image,
                                               u32 level) const override {
        return targets.ProxySize(image, level);
    }
    VideoCore::SceneResolution::Size Size() const override {
        return targets.Size();
    }
    bool Reduced() const override {
        return targets.Reduced();
    }
    u64 Generation() const noexcept override {
        return targets.Generation();
    }
    VideoCore::ReplacementView Attachment(VideoCore::ImageId image,
                                          const VideoCore::ImageViewInfo& view_info) override {
        const auto target = targets.Attachment(image, view_info);
        return {target.view, target.layout};
    }
    VideoCore::ReplacementView ClearColorAttachment(VideoCore::ImageId image,
                                                   const VideoCore::ImageViewInfo& view_info) override {
        const auto target = targets.Attachment(image, view_info, true);
        return {target.view, target.layout};
    }
    std::optional<VideoCore::ReplacementView> SampleProxy(
        const VideoCore::Image& image, const VideoCore::ImageViewInfo& view_info) override {
        if (const auto proxy = targets.SampleProxy(image, view_info)) {
            return VideoCore::ReplacementView{proxy->view, proxy->layout};
        }
        return std::nullopt;
    }
    bool ProxyCurrent(const VideoCore::Image& image, u32 level, u32 levels) const override {
        return targets.ProxyCurrent(image, level, levels);
    }
    vk::ImageLayout PrepareSample(const VideoCore::Image& image, u32 level) override {
        return targets.PrepareSample(image, level);
    }
    void SetDebug(bool debug) override {
        targets.debug = debug;
    }

private:
    SceneTargets& targets;
};

} // namespace Metal
