# bbport-mac-optimized

Bloodborne on Apple Silicon, with an optimized renderer, ultrawide support and a simple Mac launcher.

**Tested on a Mac Studio M2 Max (38-core GPU), 64 GB RAM: 30–40 FPS at full 3840×1080 (32:9), 50-60 FPS at 1920x1080 (16:9), without upscaling.** Performance varies by area and shader-cache warm-up. Other Macs, including M1, have not been tested.

Demo (1920x1080): https://www.youtube.com/watch?v=hoHo8kN9e6E

Ultrawide demo (3840x1080): https://youtu.be/ZWVT99gC3lE

## Launcher localization

This fork adds English, Simplified Chinese and Traditional Chinese launcher interfaces, with
instant switching and an independent game-language menu. The game menu includes 简体中文
(PS4 language 11) and 繁體中文 (10), preserving existing settings. Matching text/font assets
must already exist in your own game dump. Selecting Chinese also applies the bundled
`Unlock Game Region` patch to the launch image, bypassing CUSA03173's language restrictions
without modifying game files. See [language setup](docs/USER_GUIDE.md#launcher-and-game-languages).

## What's included

The `0.1.4-mem1` preview adds upstream idle command-buffer trimming, corrected macOS
graphics-memory budgeting and a pinned KosmicKrisp command-memory fix. It preserves the
Chinese launcher and perf1 shader caches. See [provenance and validation limits](docs/release/MEMORY-FIX-1.md).

The `0.1.3-perf1` preview also ports compact vertex-only render areas, selective Apple shader
precision and persistent shader-permutation fixes from Wooloomooloo2/Mark Hall. It keeps
KosmicKrisp and uses separate caches. See [scope, source commits and validation limits](docs/release/PERFORMANCE-PORT-1.md).
Gameplay FPS on a base M4 has not been measured for this preview.

- **Native Apple Silicon launcher and renderer.** The original game's x86-64 code still runs through Rosetta 2; this is not a fully native ARM64 game port.
- **16:9, 21:9 and 32:9 resolutions**, selectable in the launcher. First launch defaults to 1920×1080, then remembers your choice.
- **Optional ultrawide UI fix.** Choose your own downloaded [Nexus Mods UI pack](https://www.nexusmods.com/bloodborne/mods/202); the launcher backs up the existing files and installs it. Disabling restores those previous files. No UI pack is bundled.
- **Optional quicksave/quickload**, with remappable shortcuts: F1 settings, F2 save, F3 load. Quickload restores the latest saved checkpoint and restarts the game.
- **A standalone app:** no compiling, Xcode, Homebrew or separate Python installation needed. Extra settings are tucked away under **Advanced**.

## Quickstart

Download [Standalone launcher v.0.1.0](https://github.com/DiAvisoo/bbport-mac-optimized/releases/tag/v0.1.0)
Extract the app ZIP and open **Bloodborne Optimized.app**.

1. Select your Bloodborne game folder—the one containing `eboot.bin`.
2. Hit **Play**.
3. Have fun!

For ultrawide, enable **Ultrawide** and choose your resolution before playing. **3840×1080 (32:9)** is the tested setting above; the UI mod is optional.

**Requirements:** Apple Silicon, macOS 26 or later, Rosetta 2, and your own decrypted Bloodborne files with update 1.09 merged in. **No game files, game artwork, UI mods or saves are distributed.**

Graphical glitches after loading? Restarting the game often clears them. The first run can take longer while shaders compile. See the [user guide](docs/USER_GUIDE.md) for setup and troubleshooting.

---

- [Installation and controls](docs/USER_GUIDE.md)
- [0.1.0 release notes](docs/release/0.1.0.md)
- [Build and packaging instructions](docs/release/BUILD-OPTIMIZED.md)
- [Releases](https://github.com/DiAvisoo/bbport-mac-optimized/releases) · [Issues](https://github.com/DiAvisoo/bbport-mac-optimized/issues)

## Play

Download and extract the app archive, move **Bloodborne Optimized.app** to Applications, choose your game folder, and press **Play**. Requires macOS 26+, Apple Silicon and Rosetta 2. The app bundles its runtime and libraries; no compiling, Homebrew or separate Python installation is needed.

**Supply your own decrypted Bloodborne game files with update 1.09 merged in. No game files, artwork, saves, or shader caches are distributed.** This project is not affiliated with Sony, FromSoftware, Apple, AMD, or shadPS4.

Choose resolution and ultrawide directly in the launcher. Quicksave/quickload is optional, with click-to-remap shortcuts: **F1** configuration, **F2** quicksave, **F3** quickload. Extra settings are in **Advanced**. Existing bbport saves are shared by default; choose a copied save folder for isolated testing.

Graphical glitches after loading or changing resolution often clear after restarting the game. An occasional startup failure has also been seen at 720p. This release is ad-hoc signed and is not notarized by Apple.

## Changes and credits

The integrated source is based on the `macos-native-0.5` engine at `57b2dbd`, plus the optimized launcher/patch compiler originally developed on `macos-0.4` at `5c7b9c8`. Changes include corrected ultrawide memory sizing, cached guest-memory region lookup, empty-draw skipping, compute submission ordering, shader-cache queue safety, controller/input fixes, and fresh-save snapshots with safe restoration.

Built on the work of bmy, deadinside28's [bbport](https://github.com/deadinside28/bloodborne_pc), shadPS4, KosmicKrisp/Mesa, and their contributors. Community resolution patches retain their author metadata. See [LICENSE](LICENSE), the app's third-party notices, and the [tagged source and dependency instructions](docs/release/SOURCE.md). The [upstream README](docs/UPSTREAM-README.md) retains the original technical background.
