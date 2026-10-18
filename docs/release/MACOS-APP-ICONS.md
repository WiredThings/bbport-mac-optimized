# Launcher and game-process Dock icons (0.2.21)

The launcher uses Bloodborne's Hunter's Mark item artwork, hosted by
[Bloodborne Wiki](https://www.bloodborne-wiki.com/2015/03/hunters-mark.html). The original
406x407 image is retained in the launcher Artwork resources with its source URL and credit.
`make_app_icon.swift` scales the image without changing the rune or texture, clips rounded
corners, and adds transparent margins and a small shadow. It generates all ten 16..1024-pixel
ICNS representations. The app declares AppIcon in Info.plist and uses it for Finder and Dock;
game-folder checks never replace the running launcher icon. The previous moon-and-blade
artwork has been replaced at the owner's request. Game artwork has its own attribution and
is not relabeled as GPL code or original project artwork.

The game is a separate process (`bb-probe-metal`), so changing NSApplication in the launcher
cannot set its Dock tile. Patch 0026 instead calls `BbPort::SetGameDockIcon` on the Cocoa
branch of the real WindowSDL constructor, inside that game process. It reads the owner's
`BB_GAME_DIR/sce_sys/icon0.png` at startup. That original game cover is not bundled or modified.
[Apple's applicationIconImage property](https://developer.apple.com/documentation/appkit/nsapplication/applicationiconimage)
sets the current application's Dock icon; SDL's own Cocoa window-icon implementation uses
the same call. The launcher remains on Hunter's Mark while the game displays its own cover.

The two paths use the same 1024-pixel formatting canvas, 96-pixel transparent inset, 832-pixel
face, 184-pixel corner radius and shadow. Artwork is aspect-fitted, never stretched or cropped.
The game helper runs once on the SDL main thread, outside frame rendering. Missing/invalid
artwork retains the default icon without preventing the game from starting. It rejects files
over 16 MiB and logical image dimensions over 4096; it creates no game/Metal resource or cache.
It uses a 256-point Dock image with a 1024-pixel source representation, avoiding an unnecessary
2048-pixel HDR snapshot on a Retina screen. The tested system produced a 512-pixel Dock image.

## Verification and limits

The native `macos-app-icon-test` links the same library, runtime stubs and WindowSDL class as
the game, creates a 64x64 SDL/Metal window, and never loads guest RAM, game code or saves.
It verifies that the actual Cocoa icon image data changes, its alpha has the required clear
corners/padding and opaque center, repeated calls preserve the image, and an off-main-thread
call cannot change it. A separate missing-art run checks that the original icon is preserved.
The resulting image was visually inspected against the local original cover. Both runs passed.

The first test executable needed the same `-export_dynamic` runtime-stub option as other native
tests. Further inspection showed that AppKit mutates its cached NSImage object and snapshots
at the current display scale: pointer identity and an assumed 1024-pixel getter were invalid
test assumptions. Checks now use before/after image bytes and actual display-scaled pixels.
The failed diagnostic logs are retained locally, not counted as passing tests or game failures.

All nine performance switches remain enabled. Patch 0026 touches only window/icon setup,
AppKit linkage and its test; patches 1..25, shader/pipeline code, image copies, velocity,
occlusion, two-level page tables, memory retirement and cache budgets are unchanged. The
existing fixed-scene ABBA belongs to the previous renderer build and is recorded as prior
profile evidence, not relabeled as a new FPS measurement for this UI-only library update.

Local evidence: `outputs/metal-game-icon-tests` and `outputs/metal-game-icon-compatibility`.
Packaging checks linkage/signatures and icon declarations; installation preserves current
settings and saves and retains a rollback app.

## Quickload identity and process lifetime (0.2.32)

The game now runs inside `bin/Bloodborne Game.app`, with the stable bundle identifier
`io.github.bbport.optimized.game`. The canonical runtime stays in `bin/` for capability
discovery and build tooling; the packaged nested copy has relocated dylib references and
the same runtime instructions. The existing game-cover icon override is unchanged.

On the Metal macOS path, `supervise_restart.sh` owns game preparation and waits for each
game to exit. The runtime exits with status 75 for restart or 76 for quickload, instead
of replacing a live Cocoa process using exec. Only the quickload status forwards the
one-shot restore flag. The old PID, save lock and GPU allocations are gone before the
next game starts; mod/translation views are removed by the existing run.sh exit trap.
The supervisor also forwards termination and waits for its child. Other exits are final.
The unsupervised/Vulkan fallback retains its existing behavior.

Tests cover process separation, one-shot quickload, failure/termination propagation,
community-overlay cleanup over repeated restarts, and the real nested bundle identity.
No cache budget, resolution, graphics effect or render-pass behavior changes.

## Matching default game cover (0.2.33)

The nested game bundle now uses `tools/macos/resources/BloodborneGame.icns` as its
default AppIcon, instead of copying the launcher's Hunter's Mark. It is derived from the
local game's cover using the same aspect fit, 1024-pixel canvas, 96-pixel margin,
832-pixel face, 184-pixel corner radius and shadow as the runtime icon. The ICNS includes
all ten standard 16..1024-pixel representations. No game files are required to build it
from source: the derived icon and its attribution are tracked. The original cover PNG
is not distributed. This supersedes the original 0.2.21 note about the game cover never
being bundled.

The launcher's icon remains Hunter's Mark. Game startup and teardown can therefore fall
back to a matching cover, without briefly displaying the launcher's symbol. Runtime
cover selection, supervised restart, rendering code, settings and saves are unchanged.

The owner confirmed repeated in-game F3 restarts no longer accumulate Dock tiles in
0.2.32. For 0.2.33, packaging checks require the game icon to equal the tracked cover
and differ from the unchanged launcher icon.
