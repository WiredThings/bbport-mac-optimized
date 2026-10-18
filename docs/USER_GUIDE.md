# Bloodborne Optimized 0.2.28: user guide

[简体中文](USER_GUIDE.zh-CN.md) · [Project overview](../README.md) · [Changes and credits](THIRD_PARTY_CHANGES.md)

This guide covers the WiredThings **0.2.28 (0228) preview**. Older upstream guides describe
different backends and defaults. Download the [complete public app](https://github.com/WiredThings/bbport-mac-optimized/releases/tag/v0.2.28).

## Requirements and installation

- Apple Silicon Mac, **macOS 26 or newer**, and **Rosetta 2**. The launcher is ARM64; Metal-path
  game/renderer host code is x86-64 under Rosetta. Local validation focuses on the base M4 Mac mini
  with 10 GPU cores and 16 GB RAM; other models do not inherit a frame-rate guarantee.
- Your own decrypted Bloodborne installation, with **update 1.09 merged in**.
- Python and runtime libraries are bundled. No Xcode, Homebrew or compilation is needed to play.

Extract `Bloodborne-Optimized-0.2.28-macOS.zip`, move **Bloodborne Optimized.app** to Applications,
and open it. The app is ad-hoc signed, not notarized; if macOS blocks it, use **System Settings →
Privacy & Security → Open Anyway** for this app. Rosetta can be installed through macOS or with:

```sh
softwareupdate --install-rosetta --agree-to-license
```

For updates, quit the game and launcher before replacing the **complete app**. Saves and settings
are outside the bundle and remain in place. Keep a backup of valued progress before trying builds.

## Prepare the game folder

The selected folder must contain `eboot.bin`, `sce_sys/param.sfo` and `dvdroot_ps4`.
The expected application version in `param.sfo` is **01.09**.

If base game and update are separate, first copy the base installation into a new directory.
Overlay the *contents* of the dumped 1.09 update onto that copy, replacing matching files,
including `eboot.bin`, `sce_sys` and updated `dvdroot_ps4` content. Keep the original two folders
if you want a reversible merge. Choose the resulting full folder in the launcher; selecting only
`CUSA…-patch`, or nesting that folder inside the base, does not merge the update.

The public app supplies no game dump, decryption service, update or Chinese asset download.
A green game-folder check confirms recognized metadata; it does not validate every asset.

## First launch

1. Select **Game folder** and wait for its check.
2. Choose launcher/game language, output resolution and fullscreen mode.
3. Press **Play** (⌘R). **Stop** (⌘.) stops the game.
4. Use **Advanced** for backend, upscaling, effects, FPS, optimization controls, mods and patches.

Preparation files go to the app's data directory. First use and new scenes can compile shaders
or pipelines and load assets, causing longer startup or short stalls. This release has no
all-scenes offline-precompiled library. Existing disk caches and known pipeline entries are reused/prewarmed; previously unknown
variants can still compile at runtime. Clearing caches makes first-use compilation recur.

## Launcher and game languages

**Launcher language** changes immediately: follow system, English, 简体中文 or 繁體中文.
**Game language** is independent and applies on the next game launch. Simplified Chinese uses
PS4 language value **11**; Traditional Chinese uses **10**. Voice audio is unchanged.

The game must contain these files under `dvdroot_ps4`:

| Language | Text files | Font |
| --- | --- | --- |
| 简体中文 | `msg/zhocn/menu.msgbnd.dcx`, `msg/zhocn/item.msgbnd.dcx` | `menu/zhocn/font.gfx` |
| 繁體中文 | `msg/zhotw/menu.msgbnd.dcx`, `msg/zhotw/item.msgbnd.dcx` | `menu/zhotw/font.gfx` |

The launcher warns when required files are missing. Choosing a language cannot generate missing
text or fonts. Region-specific behavior was investigated on **CUSA03173 1.09**; other editions
have not all been checked.

For values 10/11, the launch pipeline automatically adds **Unlock Game Region** by
**Lance McDonald (manfightdragon), Kyo**. It was already in upstream's patch XML; the new part is
this branch's language-selection integration. It operates on the prepared launch image through
`patches.bin`, retaining the source game files and original Cross/Circle convention. No external
translation pack is installed. After restarting, the log should identify `Game language: 11`
(or 10) and `Unlock Game Region` in the selected patches. These log entries prove selection;
actual text/font rendering still depends on the game files and edition.

## Rendering, resolution and effects

**Display → Resolution** sets the output size. A new profile uses 1920×1080, with upscaling off;
existing settings are retained. **Advanced → Upscaler → Preset** changes the internal MetalFX
scene size. The displayed **Scene render resolution** is the effective size, including rounding.

MetalFX uses temporal upscaling. At 1920×1080 output:

| Preset | Render input |
| --- | --- |
| Native AA | 1920×1080 |
| Ultra Quality ×1.25 | 1536×864 |
| Quality ×1.5 | 1280×720 |
| Balanced ×1.7 | 1130×636 |
| Performance ×2 | 960×540 |
| Ultra Performance ×3 | 640×360 |

The preset scales each dimension. Effects and model detail remain separate choices; enabling an
optimization does not turn off effects or lower the selected resolution.

**Game object motion vectors (experimental)** adds eligible game velocity to camera motion for
MetalFX. It is enabled by default when applicable and costs GPU work/auxiliary textures. Turning
**Motion blur** off still preserves velocity generation on this route and bypasses the recognized
final blur passes. Unsupported draws retain conservative fallbacks; every game scene is not
certified for temporal quality. Restart after changing these settings.

**Metal** is the default backend. **Vulkan (compatibility)** retains the ARM64 renderer/KosmicKrisp
path, with separate caches and FSR 3.1/TAA choices. Switching backend does not convert saves.
HDR, RCAS/sharpness, preupload and Vulkan validation are shown only on Vulkan. Their saved
preferences are retained. Metal's **VSync / Unsynced** choices describe its actual presentation
behavior; legacy Mailbox/Immediate names do not imply different Metal queue implementations.

In **Frame rate**, 0 means **no additional FPS limit**, rather than a 120 Hz cap. The selected
30/60/90/unlocked game patch and presentation synchronization still affect actual frame rate.
Unlocked uses a variable-timestep patch; 60/90 use fixed-timestep patches.

## Metal optimization controls

**Advanced → Metal optimizations** exposes 25 controls: **20 ON by default**, **5 optional
experiments OFF**. Saved explicit values override defaults. Hover for technical details.

- **Verified**: adopted after the documented checks/measurements.
- **Experimental**: individual gameplay benefit or broader coverage remains uncertain, including
  some adopted-by-default choices.
- **Adopted bundle**: retained after combined-profile evaluation; this is not an isolated gain
  claim for every switch.
- **Higher memory use**: pipeline persistence has an observed memory cost.

Optional experiments are cross-chunk snapshots, fragment-parameter specialization, compute
small writes, relaxed math and binary pipeline persistence. The latter ignores archive files
when OFF, without deleting them. Idle startup-archive release remains a separate optimization
that keeps active pipelines. The retired offline-library experiment is absent.

Dependencies gate child controls without erasing their stored choice. Alpha merge additionally
requires MetalFX. **Restore recommended optimization defaults** resets this section only.
Game velocity and **Occlusion culling (experimental)** are separate controls in their respective
sections. Disable occlusion culling if it produces object/light flicker.

These memory changes keep valid-cache capacities intact; budget accounting is not a hard RAM
cap. Shader/pipeline mode changes may rebuild the corresponding cache variant. For evidence,
tradeoffs and historical scene results, see [the control notes](release/METAL-OPTIMIZATION-CONTROLS.md)
and [measurement limits](THIRD_PARTY_CHANGES.md#measurements-and-limits).

## Controls and quicksave

Controller input uses SDL gamepad mappings. The default keyboard fallback is:

| Game input | Key |
| --- | --- |
| Move / camera | W A S D / arrow keys |
| Cross: interact / Circle: dodge-run | Space / Left Shift |
| Square: item / Triangle: heal | E / Q |
| L1: transform / R1: attack | 1 / 3 |
| L2: firearm / R2: strong attack | R / F |
| L3 / R3: lock-on | Z / C |
| Options / touchpad left / touchpad right | Return / Tab / Backspace |
| D-pad up / down / left / right | I / K / J / L |
| Port settings / quicksave / quickload | F1 / F2 / F3 |

The port settings overlay also supports **L3+R3**. Click the F1/F2/F3 labels in the launcher to
remap those shortcuts; Fn/Globe may be needed for function keys. Other `key.<input>` / `pad.<input>`
bindings can be set in `bbport.ini` while the game is stopped. Existing mappings take priority
and can differ from the defaults above.

Quicksave is **off by default**. When enabled, it requests a fresh game save and snapshots the
completed on-disk writes. Quickload restores the latest snapshot and restarts the game; it is
not a full in-memory save state. Ordinary autosaving continues. F9 remains reserved for developer
input recording, but public packages do not include the private recording/route tools.

## Ultrawide, mods and community patches

Ultrawide offers 21:9/32:9 output choices and selects the matching game resolution patch and memory
size. It uses native rendering with upscaling disabled. Do not apply a competing external
resolution patch. These modes are available; arbitrary resolution/scene combinations are not
all gameplay-verified.

An optional [ultrawide UI pack](https://www.nexusmods.com/bloodborne/mods/202) must be downloaded
separately. With Ultrawide on, select its extracted folder and enable **UI fix**. The launcher
backs up affected `.gfx` files before installing them in the game directory. Disabling the fix
or Ultrawide restores those previous files, including any pre-existing UI mods. Stop the game
before changing it. Backups are kept under `optimized-launcher/ui-mod-backups` and are not released.
Unlike the runtime language patch, installing a UI fix intentionally changes managed game files.

Use **Advanced → Mods** for your own mods; later entries win file conflicts. **Third-party patches**
loads compatible shadPS4-format 01.09 patches. Resolution is managed by Display, and selecting
Metal game velocity rejects known external writes that disable its producer. The distributed XML
retains author metadata; entries present in it are not necessarily enabled or tested on this Mac.

Debug menu/fonts and free camera are optional community features, not part of automated public
play. Debug fonts are not bundled. With the [font mod](https://www.nexusmods.com/bloodborne/mods/253),
retain its layout: fonts go under `dvdroot_ps4/adhoc/font/` and shaders under
`dvdroot_ps4/adhoc/FontShader/`. `dvdroot_ps4/font/` alone is not the path validated by the runtime.
Missing fonts cause the debug patch to be skipped; missing shaders can leave it invisible.
World Warper and the private route/travel testing toolchain are excluded from the public build.

## Where files live

| Content | Default location |
| --- | --- |
| Save root, shared with existing bbport | `~/Library/Application Support/bbport/user` |
| Settings, mods/patch selection, prepared files | `~/Library/Application Support/bbport/optimized-launcher` |
| Game settings and custom bindings | `…/optimized-launcher/bbport.ini` |
| Metal disk cache | `…/optimized-launcher/gpu-cache/metal1` |
| Last game log | `…/optimized-launcher/out/last-run.log` |

Launcher preferences use the `io.github.bbport.optimized` macOS preference domain. A custom
save folder under Advanced overrides the default save root. For an isolated trial, copy your
save folder and select the copy. Do not use app deletion or shader-cache deletion to reset saves.

## Troubleshooting and reporting

| Symptom | Check |
| --- | --- |
| Game reported as 01.00 / update-only folder | Recheck the merged `eboot.bin` and `sce_sys/param.sfo`; choose the full installation |
| Missing folder / unsupported link | Confirm the game folder's complete `dvdroot_ps4` layout and that links resolve |
| Game still English | Restart; check the selected game language, text/font files and region-patch log entries |
| First/new-scene stutter | Asset loading and shader/pipeline compilation remain possible; keep caches, compare warmed scenes |
| Flicker with culling | Disable experimental occlusion culling and restart |
| Slowdown after GPU profiling | Turn GPU timing/frame statistics off for normal play; detailed timing can lower FPS |
| High memory / crackling / crash | Exit before launching a second copy; retain the log and report scene/session details. Crackling and long-session behavior are not claimed fixed |
| Source-folder warning in a complete app | In launcher Settings choose the built-in engine; if files are missing, re-extract the complete public ZIP |
| Help opens an older upstream page | Some 0.2.28 app Help links still target upstream; use this fork's README for the current guide, downloads and issues |

Report to [this fork's Issues](https://github.com/WiredThings/bbport-mac-optimized/issues), including
app version, Mac/RAM, macOS, backend, output and scene resolution/preset, effects, scene/location,
and a short relevant log excerpt. Remove private paths where appropriate. Do not upload game
files or saves. Public packages omit local benchmark/analyzer tools; runtime diagnostics are optional.

For source work, use [SOURCE.md](release/SOURCE.md), [build instructions](release/BUILD-OPTIMIZED.md)
and [third-party provenance](THIRD_PARTY_CHANGES.md). The 0.2.28 release tag/source archive is an
immutable build snapshot; current branch documentation can include later corrections.
