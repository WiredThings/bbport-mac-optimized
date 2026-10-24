// SPDX-License-Identifier: GPL-2.0-or-later

// BB_IMAGE_USAGE_LOG (P10.1 step 2): see mtl_image_usage_log.h and
// docs/metal/texture_compression_design.md.

#include "video_core/renderer_metal/mtl_image_usage_log.h"

#import <Metal/Metal.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>

#include "bbport_toggles.h"
#include "video_core/renderer_metal/vk_to_mtl.h"

namespace Metal::ImageUsageLog {

namespace {

const char* LogPath() {
    const char* env = std::getenv("BB_IMAGE_USAGE_LOG");
    return env && env[0] ? env : nullptr;
}

using Clock = std::chrono::steady_clock;

constexpr const char* kHeader =
    "state\tsnap\tid\tcreated_s\tcreated_frame\tdestroyed_s\tdestroyed_frame\tmtl_format\t"
    "vk_format\twidth\theight\tdepth\tmips\tlayers\tsamples\ttype\tds\tblock\tusage\t"
    "alloc_bytes\tviews_total\tviews_whole\tviews\tstorage_binds\tsampled_binds\t"
    "rt_binds\tupscaler_writes\tname\n";

struct NameEntry {
    MTLPixelFormat format;
    const char* name;
};
#define PF(x) {MTLPixelFormat##x, #x}
constexpr NameEntry kFormatNames[] = {
    PF(A8Unorm), PF(R8Unorm), PF(R8Unorm_sRGB), PF(R8Snorm), PF(R8Uint), PF(R8Sint),
    PF(R16Unorm), PF(R16Snorm), PF(R16Uint), PF(R16Sint), PF(R16Float), PF(RG8Unorm),
    PF(RG8Unorm_sRGB), PF(RG8Snorm), PF(RG8Uint), PF(RG8Sint), PF(R32Uint), PF(R32Sint),
    PF(R32Float), PF(RG16Unorm), PF(RG16Snorm), PF(RG16Uint), PF(RG16Sint), PF(RG16Float),
    PF(RGBA8Unorm), PF(RGBA8Unorm_sRGB), PF(RGBA8Snorm), PF(RGBA8Uint), PF(RGBA8Sint),
    PF(BGRA8Unorm), PF(BGRA8Unorm_sRGB), PF(RGB10A2Unorm), PF(RGB10A2Uint), PF(RG11B10Float),
    PF(RGB9E5Float), PF(BGR10A2Unorm), PF(RG32Uint), PF(RG32Sint), PF(RG32Float),
    PF(RGBA16Unorm), PF(RGBA16Snorm), PF(RGBA16Uint), PF(RGBA16Sint), PF(RGBA16Float),
    PF(RGBA32Uint), PF(RGBA32Sint), PF(RGBA32Float), PF(B5G6R5Unorm), PF(A1BGR5Unorm),
    PF(ABGR4Unorm), PF(BGR5A1Unorm), PF(Depth16Unorm), PF(Depth32Float), PF(Stencil8),
    PF(Depth24Unorm_Stencil8), PF(Depth32Float_Stencil8), PF(X32_Stencil8), PF(X24_Stencil8),
    PF(BC1_RGBA), PF(BC1_RGBA_sRGB), PF(BC2_RGBA), PF(BC2_RGBA_sRGB), PF(BC3_RGBA),
    PF(BC3_RGBA_sRGB), PF(BC4_RUnorm), PF(BC4_RSnorm), PF(BC5_RGUnorm), PF(BC5_RGSnorm),
    PF(BC6H_RGBFloat), PF(BC6H_RGBUfloat), PF(BC7_RGBAUnorm), PF(BC7_RGBAUnorm_sRGB),
};
#undef PF

/// {linear, sRGB} pixel format pairs: a view between the two only changes the encoding.
constexpr MTLPixelFormat kSrgbPairs[][2] = {
    {MTLPixelFormatR8Unorm, MTLPixelFormatR8Unorm_sRGB},
    {MTLPixelFormatRG8Unorm, MTLPixelFormatRG8Unorm_sRGB},
    {MTLPixelFormatRGBA8Unorm, MTLPixelFormatRGBA8Unorm_sRGB},
    {MTLPixelFormatBGRA8Unorm, MTLPixelFormatBGRA8Unorm_sRGB},
    {MTLPixelFormatBC1_RGBA, MTLPixelFormatBC1_RGBA_sRGB},
    {MTLPixelFormatBC2_RGBA, MTLPixelFormatBC2_RGBA_sRGB},
    {MTLPixelFormatBC3_RGBA, MTLPixelFormatBC3_RGBA_sRGB},
    {MTLPixelFormatBC7_RGBAUnorm, MTLPixelFormatBC7_RGBAUnorm_sRGB},
};

std::string FormatName(u32 format) {
    for (const NameEntry& entry : kFormatNames) {
        if (u32(entry.format) == format) {
            return entry.name;
        }
    }
    return fmt::format("fmt{}", format);
}

bool SrgbTwins(u32 a, u32 b) {
    for (const auto& pair : kSrgbPairs) {
        if ((u32(pair[0]) == a && u32(pair[1]) == b) || (u32(pair[1]) == a && u32(pair[0]) == b)) {
            return true;
        }
    }
    return false;
}

const char* TypeName(MTLTextureType type) {
    switch (type) {
    case MTLTextureType1D:
        return "1D";
    case MTLTextureType1DArray:
        return "1DArray";
    case MTLTextureType2D:
        return "2D";
    case MTLTextureType2DArray:
        return "2DArray";
    case MTLTextureType2DMultisample:
        return "2DMS";
    case MTLTextureTypeCube:
        return "Cube";
    case MTLTextureTypeCubeArray:
        return "CubeArray";
    case MTLTextureType3D:
        return "3D";
    case MTLTextureType2DMultisampleArray:
        return "2DMSArray";
    default:
        return "?";
    }
}

char SwizzleChar(MTLTextureSwizzle s) {
    switch (s) {
    case MTLTextureSwizzleZero:
        return '0';
    case MTLTextureSwizzleOne:
        return '1';
    case MTLTextureSwizzleRed:
        return 'r';
    case MTLTextureSwizzleGreen:
        return 'g';
    case MTLTextureSwizzleBlue:
        return 'b';
    case MTLTextureSwizzleAlpha:
        return 'a';
    }
    return '?';
}

/// "rgba" for the identity, else what each channel reads.
std::string SwizzleString(MTLTextureSwizzleChannels swizzle) {
    return {SwizzleChar(swizzle.red), SwizzleChar(swizzle.green), SwizzleChar(swizzle.blue),
            SwizzleChar(swizzle.alpha)};
}

// What one view of an image differs in.
constexpr u32 kViewFormat = 1;   ///< F: another pixel format (not only the sRGB twin)
constexpr u32 kViewSrgb = 2;     ///< S: the sRGB or linear twin of the base's format
constexpr u32 kViewSwizzle = 4;  ///< W: a swizzle other than the identity
constexpr u32 kViewType = 8;     ///< T: another texture type
constexpr u32 kViewRange = 16;   ///< R: fewer levels or slices
constexpr u32 kViewStencil = 32; ///< X: the stencil plane of a combined depth format

std::string FlagString(u32 flags) {
    std::string s;
    constexpr std::pair<u32, char> kLetters[] = {{kViewFormat, 'F'}, {kViewSrgb, 'S'},
                                                 {kViewSwizzle, 'W'}, {kViewType, 'T'},
                                                 {kViewRange, 'R'}, {kViewStencil, 'X'}};
    for (const auto& [bit, letter] : kLetters) {
        if (flags & bit) {
            s += letter;
        }
    }
    return s.empty() ? "-" : s;
}

struct Image {
    u64 id{};
    double created_s{};
    u32 created_frame{};
    double destroyed_s{};
    u32 destroyed_frame{};
    u32 pixel_format{};
    std::string vk_format;
    u32 width{}, height{}, depth{}, mips{}, layers{}, samples{};
    MTLTextureType type{};
    bool is_depth{}, has_stencil{}, block{};
    u32 usage{};
    u64 allocated{};
    std::string name;
    // Views: (pixel format, swizzle, flags) -> count.
    std::map<std::tuple<u32, std::string, u32>, u32> views;
    u32 views_total{}, views_whole{};
    u64 storage_binds{}, sampled_binds{}, rt_binds{}, upscaler_writes{};
};

std::string UsageString(u32 usage) {
    std::string s;
    if (usage & MTLTextureUsageShaderRead) {
        s += 'R';
    }
    if (usage & MTLTextureUsageShaderWrite) {
        s += 'W';
    }
    if (usage & MTLTextureUsageRenderTarget) {
        s += 'T';
    }
    if (usage & MTLTextureUsagePixelFormatView) {
        s += 'V';
    }
    return s.empty() ? "-" : s;
}

std::string Sanitized(std::string text) {
    std::ranges::replace_if(text, [](char c) { return c == '\t' || c == '\n' || c == '\r'; }, ' ');
    return text;
}

struct State {
    std::mutex mutex;
    std::unordered_map<const void*, Image> live;
    std::vector<std::string> dead; ///< the lines of the destroyed images, in order
    u64 next_id = 1;
    u32 snapshots = 0;
    Clock::time_point start = Clock::now();
    Clock::time_point last_snapshot = Clock::now();
    std::string path = LogPath() ? LogPath() : "";

    double Now() const {
        return std::chrono::duration<double>(Clock::now() - start).count();
    }

    static std::string Line(const char* state, u32 snap, const Image& image) {
        std::string views;
        for (const auto& [key, count] : image.views) {
            const auto& [format, swizzle, flags] = key;
            if (!views.empty()) {
                views += ';';
            }
            views += fmt::format("{}|{}|{}|{}", FormatName(format),
                                 swizzle == "rgba" ? "-" : swizzle, FlagString(flags), count);
        }
        if (views.empty()) {
            views = "-";
        }
        const bool dead = image.destroyed_s > 0.0;
        const char* ds = image.is_depth ? (image.has_stencil ? "DS" : "D")
                                        : (image.has_stencil ? "S" : "-");
        return fmt::format(
            "{}\t{}\t{}\t{:.3f}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t"
            "{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\n",
            state, snap, image.id, image.created_s, image.created_frame,
            dead ? fmt::format("{:.3f}", image.destroyed_s) : std::string("-"),
            dead ? fmt::format("{}", image.destroyed_frame) : std::string("-"),
            FormatName(image.pixel_format), image.vk_format, image.width, image.height, image.depth,
            image.mips, image.layers, image.samples, TypeName(image.type), ds, int(image.block),
            UsageString(image.usage), image.allocated, image.views_total, image.views_whole, views,
            image.storage_binds, image.sampled_binds, image.rt_binds, image.upscaler_writes,
            Sanitized(image.name));
    }

    /// Rewrites the file: header, the dead lines, the live images as snapshot `snapshots`. The file
    /// is replaced atomically, so a killed game leaves the last complete one. Holds the mutex.
    void WriteSnapshot() {
        ++snapshots;
        last_snapshot = Clock::now();
        std::vector<const Image*> images;
        images.reserve(live.size());
        for (const auto& [native, image] : live) {
            images.push_back(&image);
        }
        std::ranges::sort(images, {}, &Image::id);
        const std::string tmp = path + ".tmp";
        FILE* file = std::fopen(tmp.c_str(), "w");
        if (!file) {
            std::printf("Image usage log: cannot write %s\n", tmp.c_str());
            return;
        }
        std::fputs(kHeader, file);
        for (const std::string& line : dead) {
            std::fputs(line.c_str(), file);
        }
        for (const Image* image : images) {
            std::fputs(Line("live", snapshots, *image).c_str(), file);
        }
        std::fclose(file);
        if (std::rename(tmp.c_str(), path.c_str()) != 0) {
            std::printf("Image usage log: cannot replace %s\n", path.c_str());
        }
    }
};

State& GetState() {
    static State state;
    return state;
}

/// The entry of `texture`, or of the first of its parents that has one. Holds no lock on return
/// (the caller locks `state.mutex` for the entry), so the parents are walked unlocked.
const void* FindKey(State& state, const void* texture) {
    id<MTLTexture> t = (__bridge id<MTLTexture>)texture;
    const void* key = texture;
    for (int depth = 0; depth < 4 && t; ++depth) {
        {
            std::scoped_lock lock{state.mutex};
            if (state.live.contains(key)) {
                return key;
            }
        }
        t = t.parentTexture;
        key = (__bridge const void*)t;
    }
    return nullptr;
}

} // namespace

namespace detail {
const bool enabled = LogPath() != nullptr;
} // namespace detail

void Created(const void* texture, std::string_view vk_format, bool block) {
    @autoreleasepool {
        State& state = GetState();
        id<MTLTexture> t = (__bridge id<MTLTexture>)texture;
        Image image;
        image.created_s = state.Now();
        image.created_frame = BbStats::frame_number.load(std::memory_order_relaxed);
        image.pixel_format = u32(t.pixelFormat);
        image.vk_format = std::string{vk_format};
        image.width = u32(t.width);
        image.height = u32(t.height);
        image.depth = u32(t.depth);
        image.mips = u32(t.mipmapLevelCount);
        image.layers = u32(t.arrayLength);
        image.samples = u32(t.sampleCount);
        image.type = t.textureType;
        image.is_depth = VkToMtl::IsDepthPixelFormat(image.pixel_format);
        image.has_stencil = VkToMtl::HasStencilPixelFormat(image.pixel_format);
        image.block = block;
        image.usage = u32(t.usage);
        image.allocated = u64(t.allocatedSize);
        std::scoped_lock lock{state.mutex};
        image.id = state.next_id++;
        state.live[texture] = std::move(image);
    }
}

void Named(const void* texture, std::string_view name) {
    State& state = GetState();
    std::scoped_lock lock{state.mutex};
    if (const auto it = state.live.find(texture); it != state.live.end()) {
        it->second.name = std::string{name};
    }
}

void ViewMade(const void* from, const void* view) {
    @autoreleasepool {
        State& state = GetState();
        const void* key = FindKey(state, from);
        if (!key) {
            return;
        }
        std::tuple<u32, std::string, u32> view_key{};
        bool whole = view == from && key == from;
        if (!whole) {
            id<MTLTexture> v = (__bridge id<MTLTexture>)view;
            std::scoped_lock lock{state.mutex};
            const auto it = state.live.find(key);
            if (it == state.live.end()) {
                return;
            }
            Image& image = it->second;
            const u32 format = u32(v.pixelFormat);
            u32 flags = 0;
            if ((format == u32(MTLPixelFormatX32_Stencil8) ||
                 format == u32(MTLPixelFormatX24_Stencil8)) &&
                format != image.pixel_format) {
                flags |= kViewStencil;
            } else if (format != image.pixel_format) {
                flags |= SrgbTwins(format, image.pixel_format) ? kViewSrgb : kViewFormat;
            }
            const std::string swizzle = SwizzleString(v.swizzle);
            if (swizzle != "rgba") {
                flags |= kViewSwizzle;
            }
            if (v.textureType != image.type) {
                flags |= kViewType;
            }
            if (u32(v.mipmapLevelCount) != image.mips ||
                (image.type != MTLTextureType3D && u32(v.arrayLength) != image.layers)) {
                flags |= kViewRange;
            }
            ++image.views[{format, swizzle, flags}];
            ++image.views_total;
            return;
        }
        std::scoped_lock lock{state.mutex};
        if (const auto it = state.live.find(key); it != state.live.end()) {
            ++it->second.views_total;
            ++it->second.views_whole;
        }
    }
}

void Bound(const void* texture, Use use) {
    if (!texture) {
        return;
    }
    @autoreleasepool {
        State& state = GetState();
        const void* key = FindKey(state, texture);
        if (!key) {
            return;
        }
        std::scoped_lock lock{state.mutex};
        const auto it = state.live.find(key);
        if (it == state.live.end()) {
            return;
        }
        Image& image = it->second;
        switch (use) {
        case Use::Sampled:
            ++image.sampled_binds;
            break;
        case Use::Storage:
            ++image.storage_binds;
            break;
        case Use::RenderTarget:
            ++image.rt_binds;
            break;
        case Use::UpscalerWrite:
            ++image.upscaler_writes;
            break;
        }
    }
}

void Destroyed(const void* texture) {
    State& state = GetState();
    std::scoped_lock lock{state.mutex};
    const auto it = state.live.find(texture);
    if (it == state.live.end()) {
        return;
    }
    Image& image = it->second;
    image.destroyed_s = std::max(state.Now(), 1e-6);
    image.destroyed_frame = BbStats::frame_number.load(std::memory_order_relaxed);
    state.dead.push_back(State::Line("dead", state.snapshots + 1, image));
    state.live.erase(it);
}

void Tick() {
    if (!detail::enabled) {
        return;
    }
    State& state = GetState();
    std::scoped_lock lock{state.mutex};
    if (Clock::now() - state.last_snapshot >= std::chrono::seconds(30)) {
        state.WriteSnapshot();
    }
}

void Flush() {
    if (!detail::enabled) {
        return;
    }
    State& state = GetState();
    std::scoped_lock lock{state.mutex};
    state.WriteSnapshot();
}

} // namespace Metal::ImageUsageLog
