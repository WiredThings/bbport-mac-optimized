// SPDX-License-Identifier: GPL-2.0-or-later

#include "video_core/renderer_vulkan/liverpool_to_vk.h"
#include "video_core/renderer_vulkan/vk_graphics_pipeline.h"

namespace Vulkan {

AttachmentBlend AttachmentBlendState(const GraphicsPipelineKey& key, u32 i) {
    const auto& control = key.blend_controls[i];

    const auto src_color = LiverpoolToVK::BlendFactor(control.color_src_factor);
    const auto dst_color = LiverpoolToVK::BlendFactor(control.color_dst_factor);
    const auto color_blend = LiverpoolToVK::BlendOp(control.color_func);

    const auto src_alpha = control.separate_alpha_blend
                               ? LiverpoolToVK::BlendFactor(control.alpha_src_factor)
                               : src_color;
    const auto dst_alpha = control.separate_alpha_blend
                               ? LiverpoolToVK::BlendFactor(control.alpha_dst_factor)
                               : dst_color;
    const auto alpha_blend =
        control.separate_alpha_blend ? LiverpoolToVK::BlendOp(control.alpha_func) : color_blend;

    // Vulkan ignores blend factors for min/max, but a factor that zeroes one operand
    // makes the operation collapse to a plain selection: min(s, 0) is 0 and max(s, 0)
    // is s for normalized alpha. Rewrite those to the equivalent add so the result is
    // exact instead of leaving the other operand to survive.
    auto eff_src_alpha = src_alpha;
    auto eff_dst_alpha = dst_alpha;
    auto eff_alpha_blend = alpha_blend;
    if (alpha_blend == vk::BlendOp::eMin || alpha_blend == vk::BlendOp::eMax) {
        const bool takes_max = alpha_blend == vk::BlendOp::eMax;
        if (src_alpha == vk::BlendFactor::eOne && dst_alpha == vk::BlendFactor::eZero) {
            eff_alpha_blend = vk::BlendOp::eAdd;
            eff_src_alpha = takes_max ? vk::BlendFactor::eOne : vk::BlendFactor::eZero;
            eff_dst_alpha = vk::BlendFactor::eZero;
        } else if (src_alpha == vk::BlendFactor::eZero && dst_alpha == vk::BlendFactor::eOne) {
            eff_alpha_blend = vk::BlendOp::eAdd;
            eff_src_alpha = vk::BlendFactor::eZero;
            eff_dst_alpha = takes_max ? vk::BlendFactor::eOne : vk::BlendFactor::eZero;
        }
    }

    const auto color_scaled_min_max =
        (color_blend == vk::BlendOp::eMin || color_blend == vk::BlendOp::eMax) &&
        (src_color != vk::BlendFactor::eOne || dst_color != vk::BlendFactor::eOne) &&
        !key.color_buffers[i].blend_self_scale;
    const auto alpha_scaled_min_max =
        (eff_alpha_blend == vk::BlendOp::eMin || eff_alpha_blend == vk::BlendOp::eMax) &&
        (eff_src_alpha != vk::BlendFactor::eOne || eff_dst_alpha != vk::BlendFactor::eOne);

    AttachmentBlend blend{
        .enable = control.enable,
        .src_color = src_color,
        .dst_color = dst_color,
        .src_alpha = eff_src_alpha,
        .dst_alpha = eff_dst_alpha,
        .color_op = color_blend,
        .alpha_op = eff_alpha_blend,
        .unimplemented_min_max = color_scaled_min_max || alpha_scaled_min_max,
        .self_scaled = bool(key.color_buffers[i].blend_self_scale),
    };

    // The shader squares its color output for this attachment (see PsColorBuffer), so the
    // factors must not scale the operands again.
    if (blend.self_scaled) {
        blend.src_color = vk::BlendFactor::eOne;
        blend.dst_color = vk::BlendFactor::eOne;
    }

    // On GCN GPU there is an additional mask which allows to control color components exported
    // from a pixel shader. A situation possible, when the game may mask out the alpha channel,
    // while it is still need to be used in blending ops. For such cases, HW will default alpha
    // to 1 and perform the blending, while shader normally outputs 0 in the last component.
    // Unfortunatelly, Vulkan doesn't provide any control on blend inputs, so below we detecting
    // such cases and override alpha value in order to emulate HW behaviour.
    const auto has_alpha_masked_out =
        (key.cb_shader_mask.GetMask(i) & AmdGpu::ColorBufferMask::ComponentA) == 0;
    const auto has_src_alpha_in_src_blend = src_color == vk::BlendFactor::eSrcAlpha ||
                                            src_color == vk::BlendFactor::eOneMinusSrcAlpha;
    const auto has_src_alpha_in_dst_blend = dst_color == vk::BlendFactor::eSrcAlpha ||
                                            dst_color == vk::BlendFactor::eOneMinusSrcAlpha;
    if (has_alpha_masked_out && has_src_alpha_in_src_blend) {
        blend.src_color = src_color == vk::BlendFactor::eSrcAlpha ? vk::BlendFactor::eOne
                                                                  : vk::BlendFactor::eZero; // 1-A
    }
    if (has_alpha_masked_out && has_src_alpha_in_dst_blend) {
        blend.dst_color = dst_color == vk::BlendFactor::eSrcAlpha ? vk::BlendFactor::eOne
                                                                  : vk::BlendFactor::eZero; // 1-A
    }
    return blend;
}

} // namespace Vulkan
