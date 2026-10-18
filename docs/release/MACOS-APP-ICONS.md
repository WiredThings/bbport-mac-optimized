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
