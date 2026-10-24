// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <span>
#include <cstdlib>
#include "video_core/render_state.h"

namespace Metal::PassContinuation {

// Native texture views must have known, independent backing and exclude every
// attachment root, including parent views of the exact subresource.
bool TexturesExcludeAttachments(const VideoCore::RenderState&, std::span<void* const>);
bool Enabled();
// Matches the native-density resolver: at/above PS4 dimensions its scale is1.
// Never infer fractional fixed-point rounding equivalence from nonzero counts.
inline bool QueryDensityIsAdditive(std::uint64_t width, std::uint64_t height) {
    return width && height && width <= UINT64_MAX / height && width * height >= 1920ull * 1080;
}
inline bool DepthLayoutEnabled() {
    static const bool enabled = [] { const char* flag = std::getenv("BB_MTL_DEPTH_LAYOUT"); return flag && flag[0] == '1'; }();
    return enabled;
}

// Diagnostic structural classification only. A match does not authorize continuing:
// resource feedback, previous texture bindings, barriers and query state are separate.
inline bool DepthStencilLayoutOnly(const VideoCore::RenderState& old,
                                   const VideoCore::RenderState& next) {
    const auto known = [](vk::ImageLayout layout) {
        return layout == vk::ImageLayout::eDepthStencilAttachmentOptimal ||
               layout == vk::ImageLayout::eDepthStencilReadOnlyOptimal ||
               layout == vk::ImageLayout::eDepthReadOnlyStencilAttachmentOptimal ||
               layout == vk::ImageLayout::eDepthAttachmentStencilReadOnlyOptimal;
    };
    const auto& a = old.depth_stencil_attachment;
    const auto& b = next.depth_stencil_attachment;
    if (!a.image_view || !a.has_depth || !a.has_stencil ||
        a.depth_clear || a.stencil_clear || b.depth_clear || b.stencil_clear ||
        a.image_layout == b.image_layout || !known(a.image_layout) || !known(b.image_layout))
        return false;
    for (const auto& color : old.color_attachments) if (color.is_clear) return false;
    for (const auto& color : next.color_attachments) if (color.is_clear) return false;
    auto normalized = next;
    normalized.depth_stencil_attachment.image_layout = a.image_layout;
    return old == normalized;
}

inline unsigned DepthStencilWriteMask(vk::ImageLayout layout) {
    switch (layout) {
    case vk::ImageLayout::eDepthStencilReadOnlyOptimal: return 0;
    case vk::ImageLayout::eDepthAttachmentStencilReadOnlyOptimal: return 1;
    case vk::ImageLayout::eDepthReadOnlyStencilAttachmentOptimal: return 2;
    case vk::ImageLayout::eDepthStencilAttachmentOptimal: return 3;
    default: return 4; // unknown, never admitted
    }
}
inline bool DepthLayoutMayPromote(vk::ImageLayout layout) {
    return DepthStencilWriteMask(layout) < 3;
}
inline bool DepthLayoutWritePromotion(const VideoCore::RenderState& old,
                                     const VideoCore::RenderState& next) {
    if (!DepthStencilLayoutOnly(old, next)) return false;
    const unsigned a=DepthStencilWriteMask(old.depth_stencil_attachment.image_layout);
    const unsigned b=DepthStencilWriteMask(next.depth_stencil_attachment.image_layout);
    // Never merge write->readonly. A later draw in the readonly phase could start
    // sampling the attachment, after the present draw's texture check has passed.
    // Preserve the store/load boundary before every such possible reading phase.
    return a<4 && b<4 && a!=b && !(a & ~b);
}

inline bool HasClears(const VideoCore::RenderState& state) {
    for (const auto& attachment : state.color_attachments) {
        if (attachment.is_clear) return true;
    }
    return state.depth_stencil_attachment.depth_clear ||
           state.depth_stencil_attachment.stencil_clear;
}

// Only state equivalence, NOT permission to keep a physical encoder open. Callers must
// separately exclude feedback, intervening work, query boundaries and pending render tails.
// Layouts are deliberately compared, including depth/stencil read/write-role changes.
inline bool ConsumedClearBecomesLoad(const VideoCore::RenderState& old,
                                    const VideoCore::RenderState& next) {
    if (!HasClears(old) || HasClears(next)) return false;
    auto load = old;
    auto requested = next;
    for (unsigned i = 0; i < load.color_attachments.size(); ++i) {
        // Colour flags have no other meaning. Unknown encodings are not admitted.
        if (load.color_attachments[i].is_clear > 1) return false;
        load.color_attachments[i].is_clear = 0;
        load.color_attachments[i].clear_value = {};
        requested.color_attachments[i].clear_value = {};
    }
    auto& depth = load.depth_stencil_attachment;
    depth.depth_clear = false;
    depth.stencil_clear = false;
    depth.clear_value = {};
    requested.depth_stencil_attachment.clear_value = {};
    return load == requested;
}

} // namespace Metal::PassContinuation
