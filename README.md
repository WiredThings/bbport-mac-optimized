# Bloodborne Optimized for macOS

[简体中文](README.zh-CN.md) · [Download 0.2.28](https://github.com/WiredThings/bbport-mac-optimized/releases/tag/v0.2.28) · [User guide](docs/USER_GUIDE.md) · [Changes and third-party credits](docs/THIRD_PARTY_CHANGES.md)

A standalone macOS launcher and Bloodborne compatibility runtime with **direct Metal rendering**,
**temporal MetalFX**, and English, Simplified Chinese and Traditional Chinese launcher interfaces.
This is the **WiredThings fork** of [DiAvisoo/bbport-mac-optimized](https://github.com/DiAvisoo/bbport-mac-optimized).
The current public build is **0.2.28 (0228), a preview release**.

The launcher is native Apple Silicon. On the Metal path, the game and renderer host code are
x86-64 and run through **Rosetta 2**. The project builds on bbport/shadPS4 and community renderer
work; it is not an official FromSoftware/Sony PC release.

## Install and play

Requires **Apple Silicon, macOS 26+, Rosetta 2**, and your own decrypted Bloodborne game files
with **update 1.09 merged into the base game**. Python and runtime libraries are bundled;
Xcode, Homebrew and compilation are not required to play.

1. Download **Bloodborne-Optimized-0.2.28-macOS.zip** from [Releases](https://github.com/WiredThings/bbport-mac-optimized/releases/tag/v0.2.28).
2. Extract it and move **Bloodborne Optimized.app** into Applications. Quit the old app before replacing it.
3. Select the merged game folder containing `eboot.bin`, `sce_sys` and `dvdroot_ps4`.
   Select the full installation, rather than the separate `CUSA…-patch` update folder.
4. Choose launcher/game language, output resolution and fullscreen mode, then press **Play** (⌘R).
   Open **Advanced** for MetalFX, effects, frame rate and optimization controls.

The app is ad-hoc signed, not Apple-notarized. If macOS blocks it, use the app-specific
**Open Anyway** option in System Settings → Privacy & Security. See the
[guide](docs/USER_GUIDE.md) for merging updates, controls, save locations and troubleshooting.

## What this fork adds

| Area | Work on this branch |
| --- | --- |
| Languages | Three launcher languages; independent game-language selection; Chinese asset checks and automatic region-unlock integration |
| Metal backend | Integrates Huax-1839's Metal engine with the existing launcher, runtime, language, resolution and quicksave paths; Vulkan remains a compatibility option |
| Temporal inputs | Ports IFreemz's game-velocity mirror route to MetalFX; batches eligible velocity draws and preserves velocity generation when motion blur is disabled |
| Memory | Graphics-ledger budgeting without double-counting no-copy guest memory; GPU-complete retirement, idle staging backing reclamation, allocation-generation tracking and invalid sampled-copy reclamation |
| GPU address metadata | Two-level GPU address table with matching shader lookup, replacing the fixed 1 GiB flat table |
| Rendering efficiency | Sampler memoization; safe clear/load discard; direct scene-copy clears; alpha merge; compatible clear batching, copy/tiling, residency and synchronization refinements |
| Launcher | 25 Metal optimization controls with dependencies and evidence labels; backend-appropriate options; corrected presentation/limit labels; credited macOS icon formatting |
| Vulkan fallback | Retains earlier imported shader/command-memory fixes and separate caches; Vulkan-specific changes are not presented as Metal optimizations |

The optimized standalone launcher, ultrawide UI management, patch system and quicksave foundation
come from upstream. The Metal engine, original game patches and imported algorithms retain their
own authorship. [The provenance table](docs/THIRD_PARTY_CHANGES.md) separates inherited features,
third-party adaptations and this branch's integration work, with pinned source links.

## Chinese game text and community patches

Select **Game language → 简体中文 / 繁體中文** and restart the game. The corresponding Chinese
text and fonts must exist in your own game installation. This changes game text, not voice audio.

The bundled **Unlock Game Region** patch is credited to **Lance McDonald (manfightdragon) and Kyo**.
It already existed in upstream's patch database; this fork automatically selects it for Chinese
languages and applies it to the launch image. Original game files are not rewritten by this
language patch. **No translation pack, Chinese game data or fonts are supplied.**

Other bundled frame-rate, resolution, effect and debug patches keep the authors recorded in
[`patches/Bloodborne.xml`](patches/Bloodborne.xml). Optional ultrawide UI packs and debug fonts
must be obtained separately. Details: [languages](docs/USER_GUIDE.md#launcher-and-game-languages)
and [patch credits](docs/THIRD_PARTY_CHANGES.md#game-language-and-community-patches).

## Settings and performance

- **Resolution** is the output size. **Advanced → Upscaler → Preset** controls MetalFX input size;
  the launcher displays the actual scene-render resolution. MetalFX is temporal, and Native AA is 1:1.
- The 25-control Metal section defaults to **20 ON / 5 optional experiments OFF**. Saved explicit
  choices are retained. Object-motion vectors and experimental occlusion culling have separate controls.
- The five optional experiments are cross-chunk snapshot reuse, fragment-parameter specialization,
  compute-based small writes, relaxed math and binary pipeline-archive persistence. Their benefits
  remain uncertain, and some have measured memory costs. See [control evidence](docs/release/METAL-OPTIMIZATION-CONTROLS.md).
- This fork's local validation focuses on a **base M4 Mac mini, 10-core GPU, 16 GB RAM**.
  Frame rate depends on scene, render size, effects and cache warmth. **60 FPS is not guaranteed.**
  Earlier fixed-scene results and memory tradeoffs are documented in [changes and measurements](docs/THIRD_PARTY_CHANGES.md#measurements-and-limits).

Optimizations preserve the selected resolution/effects and valid cache capacities. Some features
have their own cost, including velocity textures and optional pipeline archives; memory budgeting
is not a hard process-RAM limit. First visits can still stutter during asset loading or shader/pipeline
compilation. The discarded offline-library experiment is not part of this release.

## Controls, saves and updates

**F1** settings · **F2** quicksave · **F3** quickload; click the launcher's shortcut labels to remap.
Quicksave is optional and restores a disk-save checkpoint with a game restart, rather than a
full memory save state. [Default keyboard mappings and save paths](docs/USER_GUIDE.md#controls-and-quicksave).

Saves and settings live outside the app, so replacing the complete app preserves them.
The default save root is `~/Library/Application Support/bbport/user`; existing bbport installations
share it. Choose a copied save folder for isolated trials.

## Source, credits and distribution

- [Source retrieval, pinned dependencies and building](docs/release/SOURCE.md)
- [Full branch changes and third-party attribution](docs/THIRD_PARTY_CHANGES.md)
- [0.2.28 release notes](docs/release/0.2.28.md) · [Report a problem](https://github.com/WiredThings/bbport-mac-optimized/issues)

Built on **DiAvisoo's optimized launcher, bmy/bbport-mac, deadinside28's bbport, shadPS4,
Huax-1839, IFreemz, Awakenchan, Wooloomooloo2/Mark Hall**, and the component contributors.
See the provenance table, [LICENSE](LICENSE), source-file notices and bundled third-party licenses.
The Hunter's Mark launcher artwork is credited [separately](launcher-macos/Sources/BBLauncher/Resources/Artwork/ATTRIBUTION.txt).

Public assets contain **no game executable/data, saves, shader caches, downloaded mods, World Warper,
QA apps, private route recordings or local sampling artifacts**. The launcher icon is the intentional
credited artwork asset; the separate game's Dock cover is read from the player's own game folder.
Historical upstream notes are preserved in [UPSTREAM-README](docs/UPSTREAM-README.md) and Git history.
