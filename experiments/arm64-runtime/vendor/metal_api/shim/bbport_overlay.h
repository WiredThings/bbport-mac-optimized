// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: in-game settings menu (Dear ImGui), drawn by the presenter into the swapchain image
// (Vulkan) or drawable (Metal) after the game frame, at display resolution; the backends' glue is
// renderer_vulkan/vk_overlay and renderer_metal/mtl_overlay. Insert (keyboard) or L3+R3 (gamepad)
// opens it; while it is open the game gets no pad/keyboard input. Settings: bbport_settings.h.

#pragma once

#include <functional>
#include <string>
#include "common/types.h"

union SDL_Event;
struct SDL_Window;
struct ImDrawData;

namespace BbOverlay {

/// Present thread, once: the ImGui context (style, font) and the renderer backend, which
/// `backend_init` sets up (ImGui_ImplXxx_Init; returns false on failure, the overlay stays off).
void Init(const std::function<bool()>& backend_init);

/// Window thread, for every SDL event: true when the menu consumed it.
bool HandleEvent(const SDL_Event& event);
/// Turns SDL text input on while the menu edits a value (window thread, once per poll).
void UpdateTextInput(SDL_Window* window);

/// Whether anything is drawn this frame (menu open or FPS counter on).
bool Visible();

/// The two steps of a renderer backend around one ImGui frame; both run under the ImGui mutex.
struct Backend {
    std::function<void()> new_frame;         ///< ImGui_ImplXxx_NewFrame, before ImGui::NewFrame
    std::function<void(ImDrawData*)> render; ///< records the draw data, after ImGui::Render
};

/// Present thread, every present: times the present interval (also while nothing is drawn), then,
/// when Visible(), builds the windows for an output of `width` x `height` and runs `backend`.
void Render(u32 width, u32 height, const Backend& backend);

/// The menu or the text dialog is open: the game's input is held neutral.
bool CapturesInput();

/// The settings menu is open: the window shows the system cursor over it.
bool MenuOpen();

/// Window thread: the game's text dialog (ImeDialog) state, drawn as a box over the frame.
void SetTextPrompt(bool active, const std::string& prompt, const std::string& text);

} // namespace BbOverlay
