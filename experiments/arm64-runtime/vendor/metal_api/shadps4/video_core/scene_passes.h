// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <optional>
#include <type_traits>
#include <utility>

#include "common/types.h"
#include "video_core/motion_history.h"
#include "video_core/render_state.h"
#include "video_core/scene_resolution.h"
#include "video_core/texture_cache/image.h"

// What the rasterizer core asks of the scene services (plan P4.1 R6): the reduced scene targets,
// the temporal upscaler and the camera and object motion vectors (docs/upscaler.md). The backend's
// rasterizer owns them and hands the core pointers; a backend without them passes null and the
// core then behaves as with upscaling and object motion disabled (the Null* classes below).
// Image views the services substitute for the game's own are opaque NativeView values; plain
// types only.

namespace VideoCore {

/// A function the callee only calls during the call (no allocation, unlike std::function).
template <typename Signature>
class FunctionRef;

template <typename Result, typename... Args>
class FunctionRef<Result(Args...)> {
public:
    template <typename Function>
        requires(!std::is_same_v<std::remove_cvref_t<Function>, FunctionRef> &&
                 std::is_invocable_r_v<Result, Function&, Args...>)
    FunctionRef(Function&& function) noexcept
        : object{const_cast<void*>(static_cast<const void*>(&function))},
          call{[](void* object, Args... args) -> Result {
              return (*static_cast<std::remove_reference_t<Function>*>(object))(
                  std::forward<Args>(args)...);
          }} {}

    Result operator()(Args... args) const {
        return call(object, std::forward<Args>(args)...);
    }

private:
    void* object;
    Result (*call)(void*, Args...);
};

/// An image view standing in for a game view, and the layout its image is used in.
struct ReplacementView {
    NativeView view{};
    vk::ImageLayout layout{};
};

/// A render target of the upscaler's output-size images (UI passes, display pass).
struct UpscalerTarget {
    NativeView view{};
    vk::ImageLayout layout{};
    u32 width{}, height{};
    bool native_ui = false;
};

/// The reduced scene targets: proxies of the scene's images at the render resolution of a scaled
/// preset (SceneTargets).
class ScenePasses {
public:
    virtual ~ScenePasses() = default;

    /// Whether the image (by its uid) has proxies.
    [[nodiscard]] virtual bool Tracks(u64 image_uid) const = 0;
    /// Whether the image can have a proxy at all, and whether it is a scene image (the depth
    /// starts the scene).
    [[nodiscard]] virtual bool Eligible(const Image& image) const = 0;
    [[nodiscard]] virtual bool EligibleScene(const Image& image) const = 0;
    /// The size of the proxy of mip `level` of the image.
    [[nodiscard]] virtual SceneResolution::Size ProxySize(const Image& image, u32 level) const = 0;
    /// The scene's render size; Reduced: it is below the native size.
    [[nodiscard]] virtual SceneResolution::Size Size() const = 0;
    [[nodiscard]] virtual bool Reduced() const = 0;
    /// Changes when proxies come or go (memos that name proxy views are valid while it holds).
    [[nodiscard]] virtual u64 Generation() const noexcept = 0;
    /// The proxy of an image the pass draws into (created when missing).
    virtual ReplacementView Attachment(ImageId image, const ImageViewInfo& view_info) = 0;
    // A full color load-action clear replaces all old pixels. Backends opt in; the
    // default keeps Vulkan and external scene adapters on their existing copy path.
    virtual ReplacementView ClearColorAttachment(ImageId image, const ImageViewInfo& view_info) {
        return Attachment(image, view_info);
    }
    /// The proxy view of a sampled image, when the image has a current proxy.
    virtual std::optional<ReplacementView> SampleProxy(const Image& image,
                                                       const ImageViewInfo& view_info) = 0;
    /// Whether the proxy of `levels` mips from `level` holds the image's current contents.
    [[nodiscard]] virtual bool ProxyCurrent(const Image& image, u32 level, u32 levels) const = 0;
    /// Transitions the proxy for sampling; the layout it is in.
    virtual vk::ImageLayout PrepareSample(const Image& image, u32 level) = 0;
    /// BB_SCENE_DEBUG frame: print resolves and fills.
    virtual void SetDebug(bool debug) = 0;
};

/// The temporal upscaler (TemporalUpscaler).
class UpscalerApi {
public:
    virtual ~UpscalerApi() = default;

    /// Available and switched on. Fixed for the object's lifetime (the core reads it once).
    [[nodiscard]] virtual bool Enabled() const noexcept = 0;
    /// Start of a frame in the command stream (display pass): true when the history is invalid.
    virtual bool OnFrameStart() = 0;
    /// Whether scene passes are drawn at the reduced size.
    [[nodiscard]] virtual bool RasterScaling() const = 0;
    /// The state RedirectColor/RedirectDepth/RasterScaling depend on, for memoizing a draw's
    /// render state.
    [[nodiscard]] virtual u64 RedirectState() const noexcept = 0;
    /// This frame's sub-pixel jitter in pixels; zero when off and after the upscale.
    [[nodiscard]] virtual std::array<float, 2> Jitter() const noexcept = 0;
    /// LOD bias for scene materials while a reduced scene is upscaled.
    [[nodiscard]] virtual float SceneMipBias() const = 0;

    /// A pass's first color target.
    virtual void OnColorTarget(ImageId color) = 0;
    /// A draw into a full-size RGBA16F target with the scene depth: the scene color.
    virtual void OnSceneColor(ImageId color) = 0;
    /// A blended draw into the scene color that is not a full-screen pass.
    virtual void OnBlendedSceneDraw() = 0;
    /// A full-screen pass into the scene color.
    virtual void OnSceneComposite() = 0;
    /// Before a compute dispatch: the post-processing combine shader triggers the upscale.
    virtual void OnDispatch(u64 cs_hash) = 0;
    /// After identifying a draw's targets, before drawing any UI pixels.
    virtual void OnDraw(u64 vs_hash, ImageId color, ImageId depth, bool native_viewport) = 0;

    /// Render target redirection (UI passes, display pass): false when not redirected.
    virtual bool RedirectColor(ImageId color, const ImageViewInfo& view, UpscalerTarget& target) = 0;
    virtual bool RedirectDepth(ImageId depth, UpscalerTarget& target) = 0;
    /// A sampled image replaced by the upscaled frame (the display pass).
    virtual bool RedirectSampled(ImageId image, const ImageViewInfo& info, NativeView& view,
                                 vk::ImageLayout& layout) = 0;
    /// Whether RedirectSampled would replace `image`.
    [[nodiscard]] virtual bool RedirectsSampled(ImageId image) const = 0;
};

/// Camera motion vectors (CameraMotion).
class CameraMotionApi {
public:
    virtual ~CameraMotionApi() = default;

    /// Fixed for the object's lifetime (the core reads it once).
    [[nodiscard]] virtual bool Enabled() const noexcept = 0;
    /// The scene depth of the current frame (null until a G-buffer pass).
    [[nodiscard]] virtual ImageId Depth() const noexcept = 0;
    virtual void InvalidateHistory() = 0;
    virtual void SetJitter(std::array<float, 2> value) noexcept = 0;
    /// A bound constant buffer of 864 bytes: checks the scene constant signature.
    virtual void OnConstants(const float* data) = 0;
    /// The G-buffer pass (5+ color targets): its depth is the scene depth.
    virtual void OnGBufferPass(ImageId depth, float x_sign, float y_sign) = 0;
    /// The pass copying the finished frame to the display.
    virtual void OnDisplayPass(ImageId frame) = 0;
};

/// Object motion vectors (ObjectMotion).
class ObjectMotionApi {
public:
    virtual ~ObjectMotionApi() = default;

    /// Start of a frame (display pass).
    virtual void OnFrameStart() = 0;
    virtual void InvalidateHistory() = 0;
    /// Small skeletons: whether the bone palette changed.
    virtual bool Moving(const Motion::History::GateKey& key, u64 palette) = 0;
    /// Index range and topology hash of an indexed draw, reused across frames; `scan` computes
    /// them when the cache does not have them.
    virtual Motion::IndexRangeCache::Result IndexRange(
        const Motion::IndexRangeCache::Key& key,
        FunctionRef<Motion::IndexRangeCache::Result()> scan) = 0;
    /// The motion parameters of a draw (the shader's `motion_param`).
    virtual u32 PrepareDraw(const Motion::Draw& draw) = 0;
    /// Rendering of a motion pipeline: adds the motion attachment (render-target size).
    virtual void Attach(RenderState& state, u32 width, u32 height) = 0;
};

/// The services of a backend without them: upscaling and object motion are off.
class NullScenePasses final : public ScenePasses {
public:
    bool Tracks(u64) const override { return false; }
    bool Eligible(const Image&) const override { return false; }
    bool EligibleScene(const Image&) const override { return false; }
    SceneResolution::Size ProxySize(const Image&, u32) const override { return {}; }
    SceneResolution::Size Size() const override { return {}; }
    bool Reduced() const override { return false; }
    u64 Generation() const noexcept override { return 0; }
    ReplacementView Attachment(ImageId, const ImageViewInfo&) override { return {}; }
    std::optional<ReplacementView> SampleProxy(const Image&, const ImageViewInfo&) override {
        return std::nullopt;
    }
    bool ProxyCurrent(const Image&, u32, u32) const override { return false; }
    vk::ImageLayout PrepareSample(const Image&, u32) override { return {}; }
    void SetDebug(bool) override {}
};

class NullUpscaler final : public UpscalerApi {
public:
    bool Enabled() const noexcept override { return false; }
    bool OnFrameStart() override { return false; }
    bool RasterScaling() const override { return false; }
    u64 RedirectState() const noexcept override { return 0; }
    std::array<float, 2> Jitter() const noexcept override { return {}; }
    float SceneMipBias() const override { return 0.0f; }
    void OnColorTarget(ImageId) override {}
    void OnSceneColor(ImageId) override {}
    void OnBlendedSceneDraw() override {}
    void OnSceneComposite() override {}
    void OnDispatch(u64) override {}
    void OnDraw(u64, ImageId, ImageId, bool) override {}
    bool RedirectColor(ImageId, const ImageViewInfo&, UpscalerTarget&) override { return false; }
    bool RedirectDepth(ImageId, UpscalerTarget&) override { return false; }
    bool RedirectSampled(ImageId, const ImageViewInfo&, NativeView&, vk::ImageLayout&) override {
        return false;
    }
    bool RedirectsSampled(ImageId) const override { return false; }
};

class NullCameraMotion final : public CameraMotionApi {
public:
    bool Enabled() const noexcept override { return false; }
    ImageId Depth() const noexcept override { return {}; }
    void InvalidateHistory() override {}
    void SetJitter(std::array<float, 2>) noexcept override {}
    void OnConstants(const float*) override {}
    void OnGBufferPass(ImageId, float, float) override {}
    void OnDisplayPass(ImageId) override {}
};

class NullObjectMotion final : public ObjectMotionApi {
public:
    void OnFrameStart() override {}
    void InvalidateHistory() override {}
    bool Moving(const Motion::History::GateKey&, u64) override { return false; }
    Motion::IndexRangeCache::Result IndexRange(
        const Motion::IndexRangeCache::Key&,
        FunctionRef<Motion::IndexRangeCache::Result()> scan) override {
        return scan();
    }
    u32 PrepareDraw(const Motion::Draw&) override { return 0; }
    void Attach(RenderState&, u32, u32) override {}
};

} // namespace VideoCore
