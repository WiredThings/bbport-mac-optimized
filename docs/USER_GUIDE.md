# Bloodborne Optimized: user guide

## Requirements

- Apple Silicon and macOS 26 or later. Tested on M2 Max with 64 GB RAM; this release uses M2 CPU tuning. Other models are untested.
- Rosetta 2 for the game's x86-64 code. If missing, install it through macOS or run `softwareupdate --install-rosetta --agree-to-license` in Terminal.
- Your own decrypted Bloodborne files, with update 1.09 merged into the base game. Choose the folder containing `eboot.bin`.

Python and the graphics libraries are bundled. Xcode, Homebrew and compilation are not required to play.

## Install and play

1. Extract `bbport-mac-optimized-0.1.0.zip`. Move **Bloodborne Optimized.app** into Applications. Keep an older working app under another name during testing.
2. Open it. This release is ad-hoc signed, not notarized. If macOS blocks it, attempt to open it once, then use **System Settings → Privacy & Security → Open Anyway**. Do not disable Gatekeeper.
3. Choose the game folder. Select resolution/fullscreen, then click **Play** (⌘R). **Stop** (⌘.) ends the game.

The first launch prepares game files and compiles shaders; it can take longer and stutter while new scenes are cached. No preparation files are written inside the app.

## Resolution and ultrawide

First launch defaults to **1920×1080**. Later launches remember your display choices.

For full-resolution 32:9, enable **Ultrawide** and choose **3840×1080**. The launcher selects the matching resolution/camera patch and graphics memory size automatically, with upscaling disabled. Other available wide choices include 2560×720, 2560×1080 (21:9), and 3440×1440 (21:9).

Disable Ultrawide to return to standard 16:9. Change resolution while stopped, then start again. Do not enable an additional external resolution patch at the same time.

The launcher uses the UI already in your game folder unless you enable the optional **UI fix**. With Ultrawide selected and a green game-folder check, choose the extracted [Ultrawide UI Fixes download from Nexus Mods](https://www.nexusmods.com/bloodborne/mods/202). Choosing a valid folder automatically backs up and enables its UI fix. The mod folder, its `menu` subfolder, or a parent containing one mod folder is accepted. A green check verifies readable, intact `.gfx` containers; it does not certify that the layout matches your resolution. Any aspect ratio and partial UI packs are allowed. Other file types are ignored.

**Enable UI fix** first backs up the files it will replace, then installs your selected `.gfx` files into `dvdroot_ps4/menu`. Disabling restores exactly what was present before enabling, including any prior mods, and removes files this pack added. If those previous files already contained an ultrawide UI mod, that layout remains; restore the earlier original-file backups to remove it. The launcher explains when the backed-up files already match the selected mod. Turning Ultrawide off also restores the previous files; switching between wide resolutions leaves your chosen UI enabled. Stop the game before changing these settings. Disable the current UI fix before selecting a different pack.

Backups are private to your Mac, under `optimized-launcher/ui-mod-backups`, separate for each game folder. The download may be removed after enabling; restoring needs only the verified local backup. Interrupted swaps can be recovered by disabling the fix. If another tool changes a managed file, the launcher refuses to overwrite it; resolve the conflict first. Backups, downloads and game assets are never included in the app or source release.

Reported performance is 30–40 FPS at 3840×1080 on M2 Max 64 GB. Higher resolutions cost more; 720p is available for speed at reduced image quality.

## Launcher and game languages

The **Language** section has two independent choices:

- **Launcher language**: Follow system, English, 简体中文 or 繁體中文. This changes the
  launcher immediately, including Advanced, shortcuts, folder checks, Help and the log window.
- **Game language**: the PS4 system-language value sent to Bloodborne on its next launch.
  简体中文 uses value **11**, and 繁體中文 uses **10**. Other retail language choices are
  also available. Changing this does not translate the launcher or change voice audio.

Existing game language, folder, controller and save settings are retained. A new launcher-language
preference defaults to the system language. Chinese requires the matching text archives in
`dvdroot_ps4/msg/zhocn` or `zhotw`, and `dvdroot_ps4/menu/<language>/font.gfx`. The launcher
warns if these files are missing. No translation patch or game files are downloaded or replaced.
Language availability and actual in-game rendering still depend on the player's game dump.

Translations are standard `Localizable.strings` resources under
`launcher-macos/Sources/BBLauncher/Resources/<language>.lproj`. English source strings are the
keys; preserve `%@` placeholders and use positional arguments if reordering them. To add a
launcher language, supply its resource folder, add its option/resolution in `Localization.swift`
and add its code to the packaged app's `CFBundleLocalizations`. The Swift tests check Chinese
preferences, runtime values, resource coverage, interpolation and missing-file warnings.

## Shortcuts and quicksave

The basic view shows the three configurable in-game shortcuts:

| Action | Default |
| --- | --- |
| Configuration menu | F1 |
| Quicksave | F2 |
| Quickload | F3 |

Click a shortcut, then press its replacement key. Conflicts are explained by name. F9 is reserved for developer input recording; Escape is reserved by the game/menu. Depending on keyboard settings, hold Fn/Globe when using function keys. The controller's L3+R3 configuration shortcut remains available.

Quicksave/quickload is off by default. Enable it in the basic view. Quicksave asks the game to write a fresh character/profile save, then snapshots the on-disk files once writes finish. Quickload restores the latest snapshot and restarts the game. It is **not a full memory save state**. Keep normal backups of valued progress. A failed or busy save does not replace the previous checkpoint.

## Advanced settings and saves

Open **Advanced** for save-folder selection, frame limit, effects, controller/keyboard settings, mods, external patches and diagnostics. Leave performance defaults unchanged for the tested profile. Live resolution and competing upscalers are disabled while using a wide patch.

- Default saves and shader data: `~/Library/Application Support/bbport/user`
- Launcher-generated files/settings/logs: `~/Library/Application Support/bbport/optimized-launcher`

Existing bbport saves are shared by default. For an isolated trial, copy your saves folder first and choose that copy under Advanced. The app never distributes or uploads your saves.

## Troubleshooting

- **Graphical glitches after loading:** restart the game; this often clears them. At 720p, an occasional startup failure has also occurred. This is a known issue, not a guaranteed fix.
- **Low FPS on the first run:** allow shader compilation to finish. Performance depends on the scene and display resolution.
- **Shortcut does nothing:** enable quicksave, check the legend/remap, and try Fn with function keys. Save only while in normal gameplay.
- **App runtime incomplete:** extract a fresh copy of the full ZIP; don't move individual files out of the app.

Report issues to [bbport-mac-optimized](https://github.com/DiAvisoo/bbport-mac-optimized/issues), including Mac model, macOS version, resolution and the relevant log excerpt. Avoid attaching game files or saves to public reports.

## Building and updating

Replace the app with a newer full package after testing; saves are outside it. For source builds, use [BUILD-OPTIMIZED.md](release/BUILD-OPTIMIZED.md). Source retrieval is documented in [SOURCE.md](release/SOURCE.md); license notices are bundled with the app.
