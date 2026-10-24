// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

namespace BbPort {
// Called once on SDL's Cocoa window thread, inside the game process, not the launcher.
// Reads the owner's original sce_sys/icon0.png and changes only its Dock representation.
bool SetGameDockIcon();
// A remote frontend has no game window; only its ARM child owns the Dock icon.
void HideGameProcessDockIcon();
}
