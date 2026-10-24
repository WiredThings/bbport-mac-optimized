// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string_view>

#include "common/types.h"

/// bbport: BB_IMAGE_USAGE_LOG=<path>, the inventory of P10.1 (docs/metal/texture_compression_design.md,
/// step 2): per texture of the texture cache, which Metal usages it needs (a shader write: a storage
/// binding or the upscaler's merge; a view in another pixel format or a swizzle, a sub range, the
/// stencil plane) and how it is bound. The hooks are no-ops when the variable is unset; the
/// pointers are the MTLTexture objects (`void*`, plain C++ callers). Read the TSV with
/// macos/bench/image_usage.py.
namespace Metal::ImageUsageLog {

namespace detail {
extern const bool enabled; ///< BB_IMAGE_USAGE_LOG is set (read once at start)
}

/// Whether the log is on; the hooks' callers test it before preparing their arguments.
inline bool Enabled() {
    return detail::enabled;
}

enum class Use : u8 {
    Sampled,       ///< a sampled image binding of a shader
    Storage,       ///< an image binding the shader writes (Shader::ImageResource::is_written)
    RenderTarget,  ///< a colour, depth or stencil attachment of a render pass
    UpscalerWrite, ///< the merge compute pass of the upscaler stores into it
};

/// A texture of the caches was created (Runtime::CreateImage).
void Created(const void* texture, std::string_view vk_format, bool block);
/// Runtime::SetImageName.
void Named(const void* texture, std::string_view name);
/// A view was made of `from` (a cache texture, or a view of one); `view == from` when the whole
/// texture serves as the view. Views of textures that are not in the log are ignored.
void ViewMade(const void* from, const void* view);
/// `texture` (a cache texture or a view of one: its parents are followed) is bound for `use`.
void Bound(const void* texture, Use use);
/// Runtime::DestroyImage: the texture's final line.
void Destroyed(const void* texture);
/// Once a frame: writes a snapshot every 30 s.
void Tick();
/// Writes a snapshot now (shutdown).
void Flush();

} // namespace Metal::ImageUsageLog
