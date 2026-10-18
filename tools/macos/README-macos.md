# bbport on macOS (Apple Silicon): building from source

Most people should download the app from the
[Releases page](https://github.com/bmy/bbport-mac/releases) instead; see the
[user guide](../../docs/USER_GUIDE.md). This page is for building from a checkout: to test a
branch, or to work on the port.

The game code runs as x86-64 under Rosetta 2. Vulkan runs on Metal through KosmicKrisp.
MoltenVK can't run bbport, because it has no sparse buffers. You need the game folder: Bloodborne CUSA03173, merged with
update 1.09, the one with `eboot.bin` in it.

## One-time setup

Clone the repository (the main branch is `macos-0.4`; `macos-native` has the experimental native
GPU process):

```
git clone --recursive -b macos-0.4 https://github.com/bmy/bbport-mac.git ~/Projects/bbport-mac/src
cd ~/Projects/bbport-mac/src
```

1. Install the Command Line Tools (`xcode-select --install`), Rosetta 2
   (`softwareupdate --install-rosetta --agree-to-license`) and Apple Silicon Homebrew
   (https://brew.sh).
2. Build the x86-64 dependencies into `deps-x86_64/`. This takes a while (KosmicKrisp is
   the longest part):

   ```
   bash tools/macos/setup_deps.sh
   ```

   Run it again after it changes. It skips whatever is already built.

## Building

```
bash tools/macos/build.sh            # the port: out/bb-probe and out/gpu/libbbgpu.dylib
bash tools/macos/build_launcher.sh   # the launcher app: out/bbport.app
```

To update later, press **Update** in the app, or run `bash tools/macos/update.sh`: it fetches
the latest version from GitHub and rebuilds only what changed (dependencies, the port, the app).
`bash tools/macos/update.sh <branch>` switches to another branch; in the app, set the branch in
bbport > Settings. If the app itself was rebuilt, quit and reopen it. The app remembers the checkout it was built from, and you can change
it in bbport > Settings (⌘,). If you have already picked the game folder in the app,
`build_launcher.sh` also uses the game's icon for the app.

## Playing

**From the app:** `open out/bbport.app`, or drag it to /Applications. Choose the game folder,
adjust the settings and press **Play** (⌘R). The Game Log window streams the output live. **Stop**
(⌘.) ends the game. Settings are saved when the game starts:

- The launcher's own settings go to the app's preferences
  (`defaults read io.github.bbport.mac`).
- Game settings go to `bbport.ini` in the checkout. The in-game menu reads and writes the same
  file, and other keys in it are kept.

**From Terminal:**

```
BB_GAME_DIR=~/Games/shadPS4/CUSA03173-109 bash tools/macos/run.sh
```

`run.sh` takes the same environment variables the app sets, for example `BB_FPS=30|60|90|uncap`,
`BB_UPSCALER=off|fsr3|taa|metalfx`, `BB_FULLSCREEN=1`.

### First launch: the shader warm-up

The first launch after a build, a driver update or a shader-cache reset compiles the game's
shaders. The window can stay black for a few minutes while that happens, which is normal. Later
launches load the cached pipelines at startup and are quick. The pipeline cache lives in the
saves folder (`user/` by default).

## Controls

A DualSense works over USB or Bluetooth. Keyboard layout:

| Keyboard | PS4 |
|---|---|
| W A S D | left stick |
| arrow keys | right stick |
| I J K L | D-pad |
| Space | Cross |
| Left Shift | Circle |
| E | Square |
| Q | Triangle |
| Enter | Options |
| 1 / 3 | L1 / R1 |
| R / F | L2 / R2 |
| Z / C | L3 / R3 |
| Tab / Backspace | touchpad |

The overlay menu (upscaler, FPS counter, effects) opens with **F1**, **`` ` ``** or **§**, or
**L3+R3** on a controller.

## Release builds

`bash tools/macos/package.sh <version>` turns a finished build into the release download,
`dist/bbport-mac-<version>.zip`: one self-contained `bbport.app` (launcher, engine, the x86-64
libraries and KosmicKrisp, ad-hoc signed) with a read-me. The packaged app runs its built-in
engine and keeps its data in `~/Library/Application Support/bbport` (`tools/macos/run.sh`
recognises the layout by the `.packaged` file); bbport ▸ Settings can point it at a checkout.

Versions are `<upstream bbport version>-v<build>`, and the zip is `bbport-mac-<version>.zip`.
There is a release for each new upstream bbport version (`0.4-v1`, then `0.4.1-v1` or
`0.5-v1` once it is merged), and another build of the same version (`0.4-v2`, `0.4-v3`) only
to ship a fix for something broken.

Before a new upstream version (or a changed dependency pin) is pushed or released, it gets the
security review in [docs/security-review.md](../../docs/security-review.md): two independent
reviews, of the upstream change and of ours.

Releases are made by GitHub Actions (`.github/workflows/release.yml`) on a GitHub-hosted Apple
Silicon runner. Write the notes as `docs/release/<version>.md` (otherwise `NEXT.md` is used),
then in the Actions tab choose **Mac release ▸ Run workflow**, pick the branch and type the
version (for example `0.4-v2`). The workflow builds everything, tags the commit and publishes
it as the latest release, with the zip and its SHA-256. Pushing a tag `<version>` or a branch
`release/<version>` does the same. *Run workflow* without a version, or a push to a `ci-*`
branch, only builds the zip (as a workflow artifact) to try a build.

## Troubleshooting

- Every run's full output is saved to **`out/last-run.log`**, and the run before it to
  `out/previous-run.log`. Send it when you report a problem. The Game Log window has a button
  that opens it.
- "expected a decrypted PS4 SELF or ELF": the game dump is encrypted or the folder isn't the
  merged 1.09 one. The message shows the file and its first bytes.
- The app says "not built yet": run `bash tools/macos/build.sh`.
- `swift build` fails in `build_launcher.sh`: send the whole terminal output.
- The build fails: `out/gpu-errors.txt` and `out/loader-build.log` hold the errors.
- If it crashes or freezes with recording on several threads, try `BB_VK_RECORD_THREADS=1`
  (and, separately, `BB_ASYNC_SUBMIT=0`) and say which one helped.
- If it runs slowly: turn Low Power Mode off and plug the Mac in.
- The black screen lasts much longer than usual: check whether the log is still moving. The
  pipeline preload reports its count and time there when it finishes.


## Optimized visual launcher (local build)

Build with `bash tools/macos/build_launcher.sh`, then open
`out/Blood Echoes.app`. You can copy this app into Applications; it remembers the
frontend checkout. The working native checkout must be linked at `out/native-05-checkout`
and contain `out/bb-probe`, `out/gpu/libbbgpu.dylib`, `out/gpu-arm64/bb-gpu`,
`out/gpu-arm64/libbbgpu.dylib`, and the `deps-arm64` driver. Play never recompiles the engine.
This launcher uses `tools/macos/run_optimized.sh`, not the older installed release runtime.

- Basic view: game folder, native resolution, Ultrawide, Fullscreen, optional quicksave/load,
  and a three-row shortcut legend: Configuration menu, Quicksave, Quickload.
- First launch defaults to 1920×1080; subsequent launches remember the chosen resolution.
- Ultrawide selects an exact community camera/HUD/resolution patch (21:9 or 32:9) and reserves
  9152 MiB of direct memory. Upscaling is off for these modes. 3840×1080 is tested on M2 Max;
  2560×720 at 32:9 is also available. Its patch uses 720p scene/UI viewports with the existing
  32:9 camera and HUD coordinate fixes. The other resolutions have not been benchmarked here.
  The 720p check reached gameplay after relaunch; an earlier attempt hit a renderer assertion,
  and intermittent graphical glitches were reported. Their cause remains unresolved.
- With Ultrawide selected and a verified game folder, the optional UI-mod picker accepts
  extracted `.gfx` packs, validates their container lengths/records, and shows a green check.
  Choosing a valid folder automatically backs up and enables its files; an explicit disable
  stays disabled across restarts. Any aspect ratio or subset is allowed. No assets are distributed.
- Enabling the UI fix backs up exactly the files it replaces before installing the user's
  download. Disabling, or turning Ultrawide off, restores those previous files and removes
  files the mod added. Existing mods in that snapshot are preserved; identical pre-installed UI
  files produce an explanatory status. Repeated disable never replays an inactive backup. Per-game immutable backups,
  hashes, a lock, and a persistent journal support recovery without overwriting unknown edits.
  The controls are disabled while playing or changing files. The optional download link is
  [Ultrawide UI Fixes](https://www.nexusmods.com/bloodborne/mods/202).
- Quicksave is opt-in: F2 requests a fresh checkpoint; F3 restores the latest verified checkpoint
  and restarts the game. The normal autosave remains active. Shortcuts are inactive when unchecked.
- Click any of the three shortcut keys to remap it, then press a single key. The default menu
  key is F1. Conflicts name the existing action; moving Configuration to F4 frees F1 for Save
  or Load. Escape cancels capture; Escape and the developer F9 recording key stay reserved.
  Gameplay bindings are checked for conflicts but are not listed in the basic view.
  Legacy saved S/L checkpoint mappings take priority over their keyboard gameplay actions while
  checkpoints are enabled; remapping them restores those gameplay keys.
- Advanced holds saves/language, frame rate, effects, standard-display upscaling, mods, patches,
  and diagnostics. Resolution patches in the external list are disabled in favour of Display.
- Settings/cache/logs: `~/Library/Application Support/bbport/optimized-launcher`.
  Default saves: the existing `~/Library/Application Support/bbport/user`.
  The first launch imports only game/save folder locations from the original launcher.
- Native optimization defaults match `play_m2_ultrawide.command`: empty-draw skipping, one
  recording thread, two frames ahead, GPU QoS; experimental unified memory and parked waits off.
  The default controller mapping includes paddle3/paddle4 as R1 unless already customized.
- Source update/rebuild is intentionally absent from the Play bar: the old updater would rebuild
  the 0.4 frontend without updating the separate modified native checkout.

This local app depends on the checkout. The original `package.sh` still describes the old
single-architecture release and must be updated to bundle both native and Rosetta dependencies
before publishing an optimized standalone release. Game files, saves and game artwork must
not be included in that release (the local launcher icon is generated from the selected game).

Validation: `swift test --package-path launcher-macos` and
`python3 -m unittest discover -s tests -p test_optimized_launcher.py`.

Native shortcut support: apply `tools/macos/patches/quicksave-key-remap.patch` after
`tools/macos/patches/m2-quicksave.patch` in the native checkout, then rebuild the loader and
both GPU libraries. It shares SDL physical scancodes across the Rosetta input process and
native overlay: `BB_MENU_SCANCODE`, `BB_QUICKSAVE_SCANCODE`, `BB_QUICKLOAD_SCANCODE`.
The launcher persists all three keys and passes them on every Play. When `BB_MENU_SCANCODE`
is set, only that keyboard key opens Configuration; existing shell launchers without it keep
the F1/backtick/§/Insert aliases. The controller L3+R3 menu shortcut remains available.
Verify input routing and save guards with `python3 tools/macos/test_quicksave_native.py`.
