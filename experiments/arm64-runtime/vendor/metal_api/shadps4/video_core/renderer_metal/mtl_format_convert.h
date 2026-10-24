// SPDX-License-Identifier: GPL-2.0-or-later
// The guest formats Metal has no texture for, and how transfers between their guest byte layout
// (the Vulkan format's buffer layout, what vkCmdCopyBufferToImage takes) and the Metal texture
// convert (plan P2.4 follow-up; the kernels are in mtl_format_convert.mm, the runtime's copy paths
// in mtl_runtime.mm). Plain C++.
//
//  - RGB8/RGB16/RGB32 (3 components): the texture has the RGBA format of the same type. An upload
//    spreads the texels to 4 components and writes the format's "one" into alpha (what a read of
//    an RGB format returns); a download drops alpha.
//  - D24UnormS8Uint, D16UnormS8Uint (depth aspect) and X8D24UnormPack32: the texture is
//    Depth32Float[_Stencil8]. The depth aspect of the guest layout is 4 bytes per texel for D24
//    (24 bit unorm in the low bits, the top byte unused: zero on a download) and 2 bytes for D16,
//    the Vulkan layout of those aspects; the texture holds d / (2^n - 1) as float32 and a download
//    rounds back to the nearest code, so a round trip is exact for every code. The stencil aspect
//    of D24S8 and D16S8 is a plain byte per texel, nothing converts there.

#pragma once

#include <atomic>

#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"

namespace Metal::FormatConvert {

enum class Kind : u8 {
    None,       ///< the copy needs no conversion (the blit encoder does it)
    RgbPad,     ///< 3 -> 4 components
    DepthUnorm, ///< 16 or 24 bit unorm <-> float32
};

/// What one copy of an image needs.
struct Conversion {
    Kind kind{Kind::None};
    u32 comp_bytes{};        ///< RgbPad: bytes of one component (1, 2 or 4)
    u32 one{};               ///< RgbPad: the bits of "one" in a component of the host format
    u32 depth_bits{};        ///< DepthUnorm: 16 or 24
    u32 guest_texel_bytes{}; ///< bytes of a texel in the guest layout
    u32 host_texel_bytes{};  ///< the same in the layout the Metal blit takes (tight rows)
};

/// The RGBA format Metal holds the 3-component guest format in, eUndefined for other formats.
vk::Format RgbaFormatOf(vk::Format guest);

/// D24UnormS8Uint, D16UnormS8Uint and X8D24UnormPack32.
bool IsPackedDepth(vk::Format guest);

/// Whether an image of guest format `guest` held as `host` is one of the converted kinds.
bool Convertible(vk::Format guest, vk::Format host);

/// The conversion of a copy with the given aspect of an image of guest format `guest` held as
/// `host`: Kind::None when the blit encoder can do the copy as it is.
Conversion Describe(vk::Format guest, vk::Format host, vk::ImageAspectFlags aspect);

/// How many images were found whose guest format has no way to transfer (Runtime::CopyCompatible):
/// their transfers are skipped and the texture stays as it is. Zero for every format the caches
/// use.
std::atomic<u32>& UnhandledCount();

} // namespace Metal::FormatConvert
