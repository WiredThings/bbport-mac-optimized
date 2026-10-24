// SPDX-License-Identifier: GPL-2.0-or-later
// No Vulkan counterpart (the Vulkan path has no frame interpolation): MetalFX, macOS 26+.

#import <Metal/Metal.h>
#import <MetalFX/MetalFX.h>

#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>
#include <utility>

#include "core/emulator_settings.h"
#include "video_core/renderer_metal/mtl_device_impl.h"
#include "video_core/renderer_metal/mtl_frame_interpolator.h"
#include "video_core/renderer_metal/mtl_scheduler_impl.h"

namespace Metal {
namespace {

/// What an interpolator's descriptor was made for.
struct Config {
    MTLPixelFormat color = MTLPixelFormatInvalid; ///< the display image, and the output
    MTLPixelFormat depth = MTLPixelFormatInvalid;
    u32 input_width = 0, input_height = 0; ///< the depth and motion textures
    u32 output_width = 0, output_height = 0;
    bool operator==(const Config&) const = default;
};

/// Makes an interpolator (the motion texture is RG16Float, as the scaler's) and prints how long
/// that took; nil on failure. Any thread. `id` and not the protocol type: the type is macOS 26.
id CreateInterpolator(id<MTLDevice> device, const Config& config) {
    const auto start = std::chrono::steady_clock::now();
    id interpolator = nil;
    @autoreleasepool {
        if (@available(macOS 26.0, *)) {
            MTLFXFrameInterpolatorDescriptor* descriptor = [MTLFXFrameInterpolatorDescriptor new];
            descriptor.colorTextureFormat = config.color;
            descriptor.outputTextureFormat = config.color;
            descriptor.depthTextureFormat = config.depth;
            descriptor.motionTextureFormat = MTLPixelFormatRG16Float;
            descriptor.inputWidth = config.input_width;
            descriptor.inputHeight = config.input_height;
            descriptor.outputWidth = config.output_width;
            descriptor.outputHeight = config.output_height;
            interpolator = [descriptor newFrameInterpolatorWithDevice:device];
        }
    }
    const double ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::printf("Metal: MetalFX frame interpolator %ux%u (input %ux%u, format %lu) %s (%.0f ms)\n",
                config.output_width, config.output_height, config.input_width,
                config.input_height, static_cast<unsigned long>(config.color),
                interpolator ? "ready" : "failed", ms);
    return interpolator;
}

id<MTLTexture> MakeTexture(id<MTLDevice> device, MTLPixelFormat format, u32 width, u32 height,
                           MTLTextureUsage usage, NSString* label) {
    MTLTextureDescriptor* descriptor =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
                                                           width:width
                                                          height:height
                                                       mipmapped:NO];
    descriptor.usage = usage;
    descriptor.storageMode = MTLStorageModePrivate; // the interpolator's output must be private
    id<MTLTexture> texture = [device newTextureWithDescriptor:descriptor];
    texture.label = label;
    return texture;
}

} // namespace

struct FrameInterpolator::Impl {
    Device::Impl& dev;
    Config config;       ///< what `interpolator`, `prev` and `output` are made for
    bool failed = false; ///< cannot run: no interpolated frames, no copies
    bool usage_checked = false;
    id interpolator = nil; ///< id<MTLFXFrameInterpolator>
    id<MTLTexture> prev = nil;   ///< frame N-1's image (RenderTarget|ShaderRead of the display's)
    id<MTLTexture> output = nil; ///< the interpolated frame
    bool prev_valid = false;     ///< `prev` is the frame before the one Run gets next
    bool needs_reset = true;     ///< the interpolator's history is stale: the next encode resets it

    // The creation thread writes `created` and `creation_done` under `mutex`; Poll takes them.
    // `creator` is joined by Poll and by the destructor.
    std::thread creator;
    std::mutex mutex;
    id created = nil;
    bool creation_done = false;

    explicit Impl(Device::Impl& dev_) : dev{dev_} {}

    /// Starts the creation for `config` on a thread of its own.
    void StartCreation() {
        const Config target = config;
        id<MTLDevice> device = dev.device;
        creation_done = false;
        created = nil;
        creator = std::thread([this, device, target] {
            id result = CreateInterpolator(device, target);
            std::lock_guard lock{mutex};
            created = result;
            creation_done = true;
        });
    }

    /// Takes the interpolator once its creation thread is done.
    void Poll() {
        if (!creator.joinable()) {
            return;
        }
        {
            std::lock_guard lock{mutex};
            if (!creation_done) {
                return;
            }
        }
        creator.join();
        interpolator = std::move(created);
        created = nil;
        if (!interpolator) {
            failed = true;
        }
    }
};

namespace {

/// Releases an object the GPU may still use once it has completed the open tick.
template <typename T>
void Retire(Scheduler& scheduler, T& object) {
    if (!object) {
        return;
    }
    scheduler.DeferOperation([retired = std::move(object)] { (void)retired; });
    object = nil;
}

} // namespace

bool FrameInterpolator::Requested(const void* device) {
    if (!EmulatorSettings.IsFrameInterpolationEnabled()) {
        return false;
    }
    if (@available(macOS 26.0, *)) {
        if ([MTLFXFrameInterpolatorDescriptor supportsDevice:(__bridge id<MTLDevice>)device]) {
            return true;
        }
        std::printf("Metal: BB_FRAME_INTERPOLATION=1, but this device does not support MetalFX "
                    "frame interpolation: staying off\n");
        return false;
    }
    std::printf("Metal: BB_FRAME_INTERPOLATION=1 needs macOS 26: staying off\n");
    return false;
}

FrameInterpolator::FrameInterpolator(Device& device, Scheduler& scheduler_)
    : scheduler{scheduler_}, impl{std::make_unique<Impl>(device.GetImpl())} {}

FrameInterpolator::~FrameInterpolator() {
    // The thread first (this is its only joiner); it touches the Impl. What the GPU may still
    // use goes with the first tick that completes.
    if (impl->creator.joinable()) {
        impl->creator.join();
    }
    Retire(scheduler, impl->created);
    Retire(scheduler, impl->interpolator);
    Retire(scheduler, impl->prev);
    Retire(scheduler, impl->output);
}

void FrameInterpolator::Invalidate() {
    impl->prev_valid = false;
    impl->needs_reset = true;
}

const void* FrameInterpolator::Run(const void* color, const InterpolationInputs& in,
                                   double delta_time, bool reset) {
    Impl& state = *impl;
    if (state.failed) {
        return nullptr;
    }
    id<MTLTexture> color_texture = (__bridge id<MTLTexture>)color;
    id<MTLTexture> depth_texture = (__bridge id<MTLTexture>)in.depth;
    id<MTLTexture> motion_texture = (__bridge id<MTLTexture>)in.motion;
    const Config config{color_texture.pixelFormat, depth_texture.pixelFormat, in.input_width,
                        in.input_height, u32(color_texture.width), u32(color_texture.height)};
    @autoreleasepool {
        if (!(config == state.config) || !state.prev) {
            // A new format or size (or the first frame): everything is made again, and the
            // interpolator on a thread of its own.
            if (state.creator.joinable()) {
                state.creator.join(); // rare: the size changed within a second of the start
            }
            Retire(scheduler, state.created);
            Retire(scheduler, state.interpolator);
            Retire(scheduler, state.prev);
            Retire(scheduler, state.output);
            state.config = config;
            state.usage_checked = false;
            state.prev_valid = false;
            state.needs_reset = true;
            state.prev = MakeTexture(state.dev.device, config.color, config.output_width,
                                     config.output_height, MTLTextureUsageShaderRead,
                                     @"Frame interpolation previous");
            state.output = MakeTexture(state.dev.device, config.color, config.output_width,
                                       config.output_height,
                                       MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite |
                                           MTLTextureUsageRenderTarget,
                                       @"Frame interpolation output");
            if (!state.prev || !state.output) {
                std::printf("Metal: frame interpolation: cannot allocate the textures: off\n");
                state.failed = true;
                return nullptr;
            }
            state.StartCreation();
        }
        state.Poll();
        if (state.failed) {
            return nullptr;
        }

        const void* result = nullptr;
        if (state.interpolator && state.prev_valid) {
            if (@available(macOS 26.0, *)) {
                id<MTLFXFrameInterpolator> interpolator = state.interpolator;
                if (!state.usage_checked) {
                    state.usage_checked = true;
                    const auto has = [](id<MTLTexture> texture, MTLTextureUsage usage) {
                        return (texture.usage & usage) == usage;
                    };
                    if (!has(color_texture, interpolator.colorTextureUsage) ||
                        !has(state.prev, interpolator.colorTextureUsage) ||
                        !has(depth_texture, interpolator.depthTextureUsage) ||
                        !has(motion_texture, interpolator.motionTextureUsage) ||
                        !has(state.output, interpolator.outputTextureUsage)) {
                        std::printf("Metal: frame interpolation: a texture lacks the usage "
                                    "MetalFX needs: off (color %lx/%lx, prev %lx, depth %lx/%lx, "
                                    "motion %lx/%lx, output %lx/%lx)\n",
                                    (unsigned long)color_texture.usage,
                                    (unsigned long)interpolator.colorTextureUsage,
                                    (unsigned long)state.prev.usage,
                                    (unsigned long)depth_texture.usage,
                                    (unsigned long)interpolator.depthTextureUsage,
                                    (unsigned long)motion_texture.usage,
                                    (unsigned long)interpolator.motionTextureUsage,
                                    (unsigned long)state.output.usage,
                                    (unsigned long)interpolator.outputTextureUsage);
                        state.failed = true;
                        return nullptr;
                    }
                    bool content_rect = false; // contentWidth/Height: macOS 27
                    if (@available(macOS 27.0, *)) {
                        content_rect = true;
                    }
                    if (!content_rect && (in.content_width != in.input_width ||
                                          in.content_height != in.input_height)) {
                        {
                            std::printf("Metal: frame interpolation: the scene fills %ux%u of "
                                        "%ux%u textures and this macOS cannot say so (27+): "
                                        "the padding is interpolated too\n",
                                        in.content_width, in.content_height, in.input_width,
                                        in.input_height);
                        }
                    }
                }
                interpolator.colorTexture = color_texture;
                interpolator.prevColorTexture = state.prev;
                interpolator.depthTexture = depth_texture;
                interpolator.motionTexture = motion_texture;
                interpolator.outputTexture = state.output;
                // No UI texture: the UI is part of the color image (see the header).
                interpolator.uiTexture = nil;
                interpolator.uiTextureComposited = NO;
                interpolator.deltaTime = float(delta_time);
                interpolator.nearPlane = in.near_plane;
                interpolator.farPlane = in.far_plane;
                interpolator.fieldOfView = in.vertical_fov;
                interpolator.aspectRatio =
                    in.content_height ? float(in.content_width) / float(in.content_height) : 1.0f;
                interpolator.jitterOffsetX = in.jitter[0];
                interpolator.jitterOffsetY = in.jitter[1];
                interpolator.motionVectorScaleX = in.motion_scale[0];
                interpolator.motionVectorScaleY = in.motion_scale[1];
                interpolator.depthReversed = NO;
                interpolator.shouldResetHistory = reset || in.reset || state.needs_reset;
                state.needs_reset = false;
                if (@available(macOS 27.0, *)) {
                    interpolator.contentWidth = in.content_width;
                    interpolator.contentHeight = in.content_height;
                }
                // Both read tracked textures: no encoder may be open when MetalFX encodes its own.
                scheduler.EndRendering();
                scheduler.EndEncoder();
                [interpolator
                    encodeToCommandBuffer:scheduler.GetImpl().EnsureCommandBuffer()];
                result = (__bridge const void*)state.output;
            }
        }

        // Frame N becomes the previous image of the next frame (after the interpolator read it).
        scheduler.EndRendering();
        scheduler.BlitEncoder("Frame interpolation history");
        [scheduler.GetImpl().blit copyFromTexture:color_texture toTexture:state.prev];
        scheduler.EndEncoder();
        state.prev_valid = true;
        return result;
    }
}

} // namespace Metal
