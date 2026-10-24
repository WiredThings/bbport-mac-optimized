// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the port's settings as pages of the game's own System menu ("Display", "Game effects",
// "Game patches" after "Screen/Sound"), built and drawn by the game's menu code: rows are the
// game's list, toggle and slider rows, texts come from a hook on its message lookup, the values live
// in the port and are applied as the player changes them. BB_GAME_MENU=0 leaves the menu as it is.
#pragma once

#include <cstdint>

namespace BbGameMenu {
/// Installs the hooks (the loader calls it once, before any game code runs, while the image is
/// still writable). Each hooked function is checked byte for byte against the 1.09 eboot.
void PatchImage(unsigned char* image, std::uint64_t size);

/// Applies menu changes each frame; returns true only after saving changed settings.
bool Poll();
} // namespace BbGameMenu


namespace BbGameMenu {
// Architecture boundary only. Menu definitions/settings remain in this module.
enum class CallbackABI : unsigned { Rows, TwoPointers, Lookup, Add };
struct GuestHook {
 std::uint64_t offset;
 const unsigned char* expected;
 std::uint64_t length,target,original;
};
struct GuestBridge {
 std::uint64_t (*call)(std::uint64_t,const std::uint64_t*,std::uint64_t);
 std::uint64_t (*callback)(void*,CallbackABI);
 void* (*allocate)(std::uint64_t);
 void (*release)(void*);
 bool (*readable)(const void*,std::uint64_t);
 bool (*install)(unsigned char*,std::uint64_t,GuestHook*,std::uint64_t);
};
// Must be configured once before PatchImage, before any guest code runs.
bool SetGuestBridge(const GuestBridge& bridge);
}
