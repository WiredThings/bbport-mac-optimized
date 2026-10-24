// SPDX-License-Identifier: GPL-2.0-or-later
#import <Metal/Metal.h>
#include "video_core/renderer_metal/mtl_motion_blur.h"
#include "video_core/renderer_metal/mtl_scene_targets.h"
#include "video_core/renderer_metal/mtl_scheduler_impl.h"
namespace Metal {
namespace {
id<MTLTexture> Root(id<MTLTexture> t) { while (t.parentTexture) t = t.parentTexture; return t; }
}
const char* BlurCopyName(BlurCopy r) {
    static constexpr const char* names[]={"blit","scale","identity","missing texture","texture type","sample count","color format/swizzle","overlapping alias","source usage"};
    return names[static_cast<unsigned>(r)];
}
BlurCopy CopyWithoutMotionBlur(Scheduler& scheduler, SceneTargets& targets, const void* src, const void* dst) {
    id<MTLTexture> a=(__bridge id<MTLTexture>)src, b=(__bridge id<MTLTexture>)dst;
    if (!a || !b) return BlurCopy::Missing;
    if (a.textureType!=MTLTextureType2D || b.textureType!=MTLTextureType2D) return BlurCopy::Type;
    if (a.sampleCount!=1 || b.sampleCount!=1) return BlurCopy::Samples;
    const auto x=a.swizzle, y=b.swizzle;
    if (a.pixelFormat!=b.pixelFormat || x.red!=y.red || x.green!=y.green || x.blue!=y.blue || x.alpha!=y.alpha) return BlurCopy::Format;
    if (a==b) { scheduler.FlushRenderTail(); return BlurCopy::Identity; }
    if (Root(a)==Root(b) || (a.buffer && a.buffer==b.buffer) ||
        (a.iosurface && a.iosurface==b.iosurface)) return BlurCopy::Alias;
    scheduler.EndWithBarrier();
    if (a.width==b.width && a.height==b.height) {
        // A raw texture blit also supports lean RenderTarget-only cache images.
        scheduler.BlitEncoder("Copy scene without motion blur");
        [scheduler.GetImpl().blit copyFromTexture:a sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
                                     sourceSize:MTLSizeMake(a.width,a.height,1) toTexture:b destinationSlice:0
                               destinationLevel:0 destinationOrigin:MTLOriginMake(0,0,0)];
        scheduler.EndWithBarrier();
        return BlurCopy::Blit;
    }
    if (!(a.usage & MTLTextureUsageShaderRead)) return BlurCopy::Usage;
    targets.CopyTextures(src,dst,"Copy scene without motion blur scale");
    scheduler.EndWithBarrier();
    return BlurCopy::Scale;
}
}
