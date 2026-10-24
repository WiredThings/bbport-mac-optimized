# Development-only native map travel

This is a minimal macOS test adaptation of the World Warper in
[visy/bloodborne_pc_windows_port](https://github.com/visy/bloodborne_pc_windows_port),
commit `7baff27fd815eb95aeb8545aafc72989aed195dd`. Upstream `gpu/shim/party/world_warper.cpp`
provides the FlipperUpdate main-thread hook, native lamp warp entry and load/arrival state
machine. `game_state.cpp` supplies the documented 1.09 loading/world/map reads (it credits
droogie/bbhost). These development sources are GPL-3.0-or-later; see LICENSE.

The adaptation does not import the tour's gear, enemy, item, time-of-day, cutscene, co-op,
shader-generation, retry/restart or save progression logic. No renderer or reclamation policy
is changed. It retains a single executable trampoline page, fixed status buffers and a small
stack frame; it does not allocate any GPU resource or maintain a resource cache.

## Build and release boundary

`build_dev_world_warper.py --app WORKSPACE/outputs/FRESH/Developer.app --build-dir FRESH_BUILD
--deps X86_DEPS` builds a release reference first. Its instruction section must match the
installed runtime. Only then is a separate `BB_DEV_WORLD_WARPER` target linked, with these
sources. Default probe builds do not compile or call the tool; the C source rejects compilation
without the development define. The developer app has a separate bundle identifier and a
DEVELOPMENT-WORLD-WARPER.json marker. The original app and Metal renderer are not replaced.

Production packaging rejects both the developer marker and an instrumented probe, including
an explicit `--metal-probe` override. Normal Play clears automation variables. Do not use the
developer app as a production baseline, and do not add the feature to the launcher UI.

## Private run

Use the existing frozen benchmark cohort and `benchmark.py run --app DEVELOPER_APP
--control-session --world-warper --minimal-stats --mute`. Guards, ownership/cleanup and save
fingerprints still apply. The tool additionally requires file-only pad input, quicksave off,
an owned private-user marker and a command directory under that user's private directory.
The host arms it only after its usual stable-scene and shader-warmup checks.

The control client's `travel` accepts only ReturnPointParam rows 2102950 (Dream), 2302950
(Old Yharnam), 2412951 (Central Yharnam). It calls the game's normal lamp loader on the game
thread. Byte signatures are checked before patching at startup; altered code is rejected.
The hook preserves general entry registers, flags, YMM registers, x87 and MXCSR, then executes
the original checked 17-byte prologue. Existing memory allocation hooks occupy other sites.

Each move needs a loading observation, the correct map id, at least 30 fresh native samples
and three stable seconds. The host also requires new rendered world frames. The result records
whether the world disappeared. Missing loading, wrong maps, timeouts or failed commands never
become successful trips. Maximum 24 moves / 1800 seconds per process; existing host critical
pressure, swap growth, footprint and lock/sleep guards remain unchanged.

## Evidence boundaries

Arrival proves a native scene transition, not complete cache eviction. Existing resource
deallocation counters must also be checked, and shared/unknown guest ownership is not permission
to free a resource. Static FPS, loading and diagnostic readback phases are distinct workloads.
The first development boot failed before any travel; do not count it as a comparison or claim
the startup cause is proven. The next build delays object reads until the scene is validated.

## Automated archive comparison

From the repository root, run:

```sh
python3 tools/macos/dev/compare_archive.py \
  --app /absolute/workspace/outputs/Bloodborne\ Developer.app \
  --seed-cohort /absolute/private/frozen-cohort \
  --cache-seed /absolute/private/frozen-cache \
  --workspace /absolute/private/fresh-comparison
```

The seed must be an existing benchmark cohort whose saved position is in Old Yharnam.
This script does not prepare or modify the normal save. It restores the same frozen cache
before each group, then performs three Old Yharnam / Dream round trips with neutral settling.
It changes only `BB_MTL_PERSIST_WARM_ARCHIVE` between groups. Per-stop category totals,
native loading evidence, release counters, cleanup and source-save checks remain in the
comparison directory. OFF then ON is an exploratory memory comparison, not an FPS ABBA.

If a controller-only failure interrupts ON after OFF has passed, `--resume-on` may preserve
that valid OFF result and restart ON from its original seed. It verifies binary and cohort
identity before doing so, retains the interrupted artifacts, and never resumes a partial trip.
External changes to normal launcher preferences are recorded and preserved; the tool does
not restore them or substitute them for the frozen private configuration.
