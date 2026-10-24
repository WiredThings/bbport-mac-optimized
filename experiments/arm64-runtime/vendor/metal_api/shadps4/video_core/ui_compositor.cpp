// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_core/ui_compositor.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include <fmt/format.h>

#include "bbport_toggles.h"
#include "video_core/ui_composition.h"

namespace VideoCore {

UiCompositor::UiCompositor(UiCompositionBackend& backend_, UiCompositorEnv env_)
    : backend{backend_}, env{std::move(env_)},
      ui_trace_on{std::getenv("BB_UI_TRACE") != nullptr} {
    if (const char* hash = std::getenv("BB_UI_TRIGGER_VS")) {
        ui_trigger_vs = std::strtoull(hash, nullptr, 16);
    }
    if (const char* res = std::getenv("BB_OUTPUT_RES")) {
        u32 w = 0, h = 0;
        if (std::sscanf(res, "%ux%u", &w, &h) == 2 && w && h) {
            output_width = w;
            output_height = h;
        }
    }
    // Explicit resolution overrides retain the old compatibility path. Normal presets
    // keep guest allocations/UI native and change only host raster targets at frame boundaries.
    scaled_session = std::getenv("BB_RENDER_RES") && std::getenv("BB_RENDER_RES")[0];
    if (const char* res = std::getenv("BB_RENDER_RES")) {
        u32 w = 0, h = 0;
        if (std::sscanf(res, "%ux%u", &w, &h) == 2 && w && h) {
            render_width = w;
            render_height = h;
        }
    }
}

void UiCompositor::OnFrameStart() {
    // Diagnostics BB_UI_TRACE=1: the draws of frames that ended without the output-size UI
    // (their display buffer is shown as the game drew it, at its own size), and of one that had it.
    if (ui_trace_on) {
        static int bad = 0, good = 0;
        if ((!ui_phase && Scaled() && good > 0 && bad < 12) || (ui_phase && good < 1)) {
            ++(ui_phase ? good : bad);
            std::printf("UI trace: frame %llu %s:%s\n",
                        (unsigned long long)BbStats::frame_number.load(std::memory_order_relaxed),
                        ui_phase ? "with the output-size UI" : "WITHOUT it", ui_trace.c_str());
        }
        ui_trace.clear();
    }
    // The display pass of a frame with the output-size UI reads the UI image.
    display_redirect = ui_phase;
    ui_phase = false;
    ui_read_barrier = false;
}

bool UiCompositor::OnDraw(u64 vs_hash, ImageId color, ImageId depth, bool native_viewport,
                          bool temporal_ready, bool temporal_active,
                          FunctionRef<void()> run_temporal) {
    // bbport (issue #67, patch by bmy): a UI draw without a color target (a Scaleform mask, color
    // writes off) writes the stencil the UI's next draws test, so it must go into the UI's
    // output-size depth like them. Left in the guest depth, the mask was lost when the next UI
    // draw switched to the UI depth (PrepareUiDepth clears its stencil): masked images (the
    // loading screen's item picture, cut out of a sheet) vanished.
    if (ui_phase && !color && depth && depth != ui_depth && ui_color) {
        const UiImageInfo depth_image = env.image_info(depth);
        const UiImageInfo ui_target = env.image_info(ui_color);
        if (depth_image.width == ui_target.width && depth_image.height == ui_target.height) {
            EnsureImages(ui_width, ui_height, ui_format, depth_image.format);
            PrepareDepth(depth);
            ui_depth = depth;
        }
        return false;
    }
    if (!Scaled() || !color) {
        return false;
    }
    if (ui_phase) {
        // A UI movie can start without stencil and enable it for later text/masks.
        if (color == ui_color && depth && depth != ui_depth) {
            EnsureImages(ui_width, ui_height, ui_format, env.image_info(depth).format);
            PrepareDepth(depth);
            ui_depth = depth;
        }
        return false;
    }
    const UiImageInfo image = env.image_info(color);
    const bool movie = vs_hash == ui_trigger_vs || UiComposition::MovieShader(vs_hash) ||
                       (scaled_session && native_viewport);
    if (ui_trace_on && ui_trace.size() < 1500) {
        ui_trace += fmt::format(" [vs {:x} {}x{} fmt {} movie {} display {}]", vs_hash, image.width,
                                image.height, u32(image.format), movie ? 1 : 0,
                                env.is_display_buffer(image.address) ? 1 : 0);
    }
    const bool ui_draw = movie &&
        (image.format == vk::Format::eR8G8B8A8Unorm || image.format == vk::Format::eR8G8B8A8Srgb) &&
        RenderTarget(image.width, image.height) && !env.is_display_buffer(image.address);
    switch (UiComposition::Choose(Scaled(), ui_draw, temporal_ready, temporal_active)) {
    case UiComposition::Background::None:
        return false;
    case UiComposition::Background::Temporal:
        run_temporal();
        if (ui_phase) {
            return false;
        }
        // A failed FSR dispatch must not force UI text back to the scene resolution.
        [[fallthrough]];
    case UiComposition::Background::Copy: {
        // Copy only the background already drawn. The subsequent menu/HUD/text draws execute
        // directly at output resolution, including frames with no scene or camera at all.
        const vk::Format depth_format =
            depth ? env.image_info(depth).format : vk::Format::eD32SfloatS8Uint;
        EnsureImages(output_width, output_height, image.format, depth_format);
        PrepareDepth(depth);
        backend.CopyBackground(color, image.width, image.height);
        Begin(color, depth);
        return true;
    }
    }
    return false;
}

void UiCompositor::EnsureImages(u32 width, u32 height, vk::Format color, vk::Format depth) {
    const bool resized = width != ui_width || height != ui_height;
    const bool new_color = ui_format == vk::Format::eUndefined || resized || color != ui_format;
    const bool new_depth =
        ui_depth_format == vk::Format::eUndefined || resized || depth != ui_depth_format;
    if (!new_color && !new_depth) {
        return;
    }
    backend.EnsureUiImages(width, height, color, depth, new_color, new_depth);
    ui_width = width;
    ui_height = height;
    if (new_color) {
        ui_format = color;
    }
    if (new_depth) {
        ui_depth_format = depth;
    }
    // depth_blit: the UI depth is copied from the scene (else only cleared); the backend decides.
    std::printf("UI: native composition %ux%u (scene %ux%u), independent of FSR history, "
                "depth_blit %d\n",
                width, height, render_width, render_height,
                int(backend.CanCopyDepth(ui_depth_format)));
}

void UiCompositor::PrepareDepth(ImageId depth) {
    // Copy depth if the backend can; otherwise just clear it (UI depth test still works).
    const bool copy = depth && backend.CanCopyDepth(env.image_info(depth).format);
    backend.PrepareUiDepth(depth, copy);
}

bool UiCompositor::RedirectColor(ImageId color, const ImageViewInfo& view_info,
                                 UpscalerTarget& target) {
    if (ui_phase && color == ui_color) {
        const ReplacementView view = backend.UiColorView(view_info.format, {});
        target = {view.view, view.layout, ui_width, ui_height, true};
        return true;
    }
    const UiImageInfo image = env.image_info(color);
    if (!env.is_display_buffer(image.address)) {
        display_redirect = false;
        return false;
    }
    std::scoped_lock lock{display_mutex};
    auto& display = displays[image.address];
    if (!display_redirect) {
        display.valid = false; // not upscaled: the presenter shows the guest buffer
        return false;
    }
    const ReplacementView view = backend.DisplayTarget(image.address, image.format,
                                                       view_info.format, ui_width, ui_height);
    display.format = image.format;
    display.width = ui_width;
    display.height = ui_height;
    display.valid = true;
    target = {view.view, view.layout, ui_width, ui_height};
    return true;
}

bool UiCompositor::RedirectDepth(ImageId depth, UpscalerTarget& target) {
    if (!ui_phase || depth != ui_depth) {
        return false;
    }
    const ReplacementView view = backend.UiDepthView();
    target = {view.view, view.layout, ui_width, ui_height, true};
    return true;
}

bool UiCompositor::RedirectSampled(ImageId image, const ImageViewInfo& info, NativeView& view,
                                   vk::ImageLayout& layout) {
    if (!display_redirect || image != ui_color) {
        return false;
    }
    if (!ui_read_barrier) {
        ui_read_barrier = true;
        backend.UiReadBarrier();
    }
    const ReplacementView replaced = backend.UiColorView(info.format, info.mapping);
    view = replaced.view;
    layout = replaced.layout;
    return true;
}

bool UiCompositor::DisplayOverride(VAddr address, FunctionRef<void(vk::Format, u32, u32)> use) {
    std::scoped_lock lock{display_mutex};
    const auto it = displays.find(address);
    if (it == displays.end() || !it->second.valid) {
        return false;
    }
    use(it->second.format, it->second.width, it->second.height);
    return true;
}

bool PresentDumpDue() {
    // Diagnostic capture of the actual completed display buffer, including native UI.
    static const char* dump = std::getenv("BB_PRESENT_DUMP_TRIGGER");
    static const int dump_count = [] {
        const char* env = std::getenv("BB_PRESENT_DUMP_COUNT");
        return env ? std::max(1, std::atoi(env)) : 1;
    }();
    static int remaining = 0;
    if (dump && remaining == 0 && std::remove(dump) == 0) {
        remaining = dump_count;
    }
    if (remaining == 0) {
        return false;
    }
    --remaining;
    return true;
}

} // namespace VideoCore
