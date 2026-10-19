# Blood Echoes 0.2.39: user guide

[简体中文](USER_GUIDE.zh-CN.md) · [Project overview](../README.md) · [Changes and credits](THIRD_PARTY_CHANGES.md)

This guide covers the WiredThings **0.2.39 (0239) preview**. Older upstream guides describe
different backends and defaults. Download the [complete public app](https://github.com/WiredThings/blood-echoes/releases/tag/v0.2.39).

## Requirements and installation

- Apple Silicon Mac, **macOS 26 or newer**, and **Rosetta 2**. The launcher is ARM64; Metal-path
  game/renderer host code is x86-64 under Rosetta. Local validation focuses on the base M4 Mac mini
  with 10 GPU cores and 16 GB RAM; other models do not inherit a frame-rate guarantee.
- Your own decrypted Bloodborne installation, with **update 1.09 merged in**.
- Python and runtime libraries are bundled. No Xcode, Homebrew or compilation is needed to play.

Extract `Blood-Echoes-0.2.39-macOS.zip`, move **Blood Echoes.app** to Applications,
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

The public app contains no game dump, decryption service, update or Chinese asset payload.
Optional community translation is downloaded separately from its author.
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

## Online play (experimental)

Online defaults OFF. With the Metal backend, open **Online**, sign in through The Hunter’s Dream device page, then start the game in online mode. Online and offline now use the same game save directory. Before online play, existing checkpoints are verified and archived, then replaced with a fresh baseline from the committed game save. F2 can save in either mode. F3 always exits the old game and restores in a new offline process, with network credentials and routes removed. It stays offline across retries and subsequent launches until you explicitly enable online again. The quicksave toggle is available in both modes. Manually switching offline creates a fresh checkpoint; an F3 restore preserves its retry checkpoint. Legacy formal `online-user` saves are backed up once: if online is selected at migration, its progress becomes the shared save; otherwise the current formal save remains active. Both originals are preserved in backups. QA saves never participate in this decision.

Device sign-in, the server announcement and chalice search/join have been exercised. Direct glyph lookup, co-op and invasions remain unvalidated. See [online setup, credits and test boundaries](ONLINE.md).

## Launcher and game languages

**Launcher language** changes immediately: follow system, English, 简体中文 or 繁體中文.
**Game language** is independent and applies on the next game launch. Simplified Chinese uses
PS4 language value **11**; Traditional Chinese uses **10**. Voice audio is unchanged.
After choosing either, **Chinese text** offers official region unlock or community retranslation.
The control is hidden for all other languages. Existing installations default to official mode.

### Official Chinese (region unlock)

This mode uses the game’s built-in text and fonts; no separate font pack is required.

In official mode, values 10/11 automatically add **Unlock Game Region** by
**Lance McDonald (manfightdragon), Kyo**. It was already in upstream's patch XML; the new part is
this branch's language-selection integration. It operates on the prepared launch image through
`patches.bin`, retaining the source game files and original Cross/Circle convention. This mode installs no external
translation pack. After restarting, the log should identify `Game language: 11`
(or 10) and `Unlock Game Region` in the selected patches. The launcher does not request any external font or translation files in official mode.

### Community retranslation (GOTY English replacement)

1. Choose the desired Chinese game language, then **Chinese text → Community retranslation**.
2. Open **Download from Nexus Mods** and obtain **Chinese Retranslation GOTY zh-cn** (Simplified)
   or **GOTY zh-tw** (Traditional) from [Supuritto's mod page](https://www.nexusmods.com/bloodborne/mods/146?tab=files).
   Use the GOTY packages rather than the generic Chinese/Japanese-dub package. The western
   GOTY edition does not gain Japanese voice audio from this translation.
3. Click **Import GOTY package…**, choose the ZIP or extracted folder, and wait for **Translation verified**.
   Simplified and Traditional packages are imported separately; select each language to import its file.
4. Start the game. The log identifies `Chinese source: community`, the requested Chinese value,
   and the effective English US `BB_LANGUAGE=1`. Region unlock is skipped for this route.

Only `menu/{engus,enggb}/font.gfx` and `msg/{engus,enggb}/{item,menu}.msgbnd.dcx` are imported.
Private copies and SHA-256 manifests live in `optimized-launcher/chinese-translations` under
Application Support. They remain outside the app/repository and survive app updates. Each import
creates a new copy, retaining earlier imports; remove old copies only when no preference uses them.
The import rejects unsafe ZIP paths, links, duplicate assets, incomplete packages and a detected
Chinese-script mismatch. It verifies all six hashes again before each launch, without per-frame work.
For English-only third-party archives without script markers, select the author's matching GOTY variant.

The dedicated translation layer is applied last, independent of the general Mods switch. It wins
text/font collisions; other mods remain loaded. Explicit known **Unlock Game Region** selections
are skipped for that launch without altering their saved selection; renamed patches overlapping
its addresses cause a clear conflict error. Switching to official Chinese or a non-Chinese language
omits this layer and uses the original assets again. The importer does not back up or rewrite the
game because its source files are never replaced. Saves, graphics options and renderer patches are unchanged.

The author prohibits redistribution; the app supplies the integration and download link, not the
translation/font payload or Nexus login credentials. Text quality is a preference: this is a
community retranslation, not the official localization shipped with Asian editions.

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

Keep **Model detail → As in the game (0)** for complete scene geometry. Forcing Lower (1)
or Lowest (2) can omit walls or floors in some areas. A same-save Metal comparison in the
Old Yharnam corridor reproduced missing surfaces with 1, restored them with 0, then
reproduced the missing surfaces when returning to 1. This also occurred at native 1080p
with MetalFX and occlusion culling off; a resolution or upscaler change alone does not fix it.
MetalFX has its own reconstruction, motion-vector and scene-copy costs. A lower
input resolution does not guarantee higher FPS; compare at the same location,
model detail and effects. If native rendering is faster, leaving MetalFX off is
a supported configuration.

The launcher and new M2 shortcut profiles now use 0 by default. Existing selections are
retained; change the setting and restart if you encounter this symptom.

The [Nexus #163 V8 package](https://www.nexusmods.com/bloodborne/mods/163?tab=files)
contains a high-resolution fix identical to the bundled **Increased Graphics Heap Sizes**
patch. It is separate from the FPS patch and requires a larger direct-memory pool. The
package also contains older FPS patches that overlap the current **Uncap FPS++** patch;
do not install or enable the entire XML as a replacement. On this 16 GB M4, the isolated
large-heap trial reached critical memory pressure before visual validation and was stopped.
It is not enabled for the missing-wall fix.


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

## Save Manager

Open **Save & load → Save Manager** while the game is closed. **Game saves** lists verified full backups; **Quick checkpoints** lists active checkpoints and archived checkpoint collections. The underlying character/profile files are the same game format; checkpoints additionally have a manifest and an active-checkpoint pointer. Restore whole saves, not individual character files.

- Back up current saves, import a `savedata` folder as a backup, export a verified backup, or reveal the save/backup directories.
- Every restore first backs up current game saves and checkpoints. A damaged backup or an occupied save lock blocks replacement.
- Checkpoints can be created in either mode while the game is closed. Every restore switches the launcher to offline; restoring a normal save also replaces stale checkpoint pointers with a fresh checkpoint. Server-side messages, statistics and other completed online actions cannot be rolled back by a local restore.
- Backups live in `optimized-launcher/save-backups/<save-folder-id>` and are not automatically pruned. Export important backups to another disk. Shader caches and credentials are excluded.
- Isolated QA saves and developer mods remain development-only; the public app never discovers or imports them.

The online service is The Hunter’s Dream. Click the displayed device code to copy it into the authorization page. Existing THD sign-in is retained. See [online setup and limitations](ONLINE.md).

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
| Help opens an older upstream page | Update to 0.2.39; its Help and About links target this fork, with a Chinese guide for Chinese launcher languages |

Report to [this fork's Issues](https://github.com/WiredThings/blood-echoes/issues), including
app version, Mac/RAM, macOS, backend, output and scene resolution/preset, effects, scene/location,
and a short relevant log excerpt. Remove private paths where appropriate. Do not upload game
files or saves. Public packages omit local benchmark/analyzer tools; runtime diagnostics are optional.

For source work, use [SOURCE.md](release/SOURCE.md), [build instructions](release/BUILD-OPTIMIZED.md)
and [third-party provenance](THIRD_PARTY_CHANGES.md). The 0.2.39 release tag/source archive is an
immutable build snapshot; current branch documentation can include later corrections.

Blood Echoes was previously named Bloodborne Optimized. The app and repository names
changed; bundle identifiers, preferences and existing save/data paths are retained.
No migration is required.
