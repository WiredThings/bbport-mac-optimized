# Separate ARM64 Metal host

This development path keeps Bloodborne and the runtime in an x86-64 Rosetta process,
while a separate ARM64 `bb-gpu` runs the command processor, caches, shader translator,
Metal renderer and window. It reuses the existing Vulkan process transport; it does
not make the game itself ARM-native. Version 0.2.50 enables the native host on supported macOS 27+ packages. The
launcher’s **ARM64 renderer host** switch returns to the in-process Metal renderer.
macOS 26 uses that fallback automatically; the packaged ARM dependency closure requires
macOS 27. The launcher itself remains native on both paths.

## Ownership and compatibility

- The game process retains executable hooks, save/quickload operations, networking,
  gamepads and the authoritative guest allocation/free tracker.
- Both processes map the same sparse guest-memory pool. The frontend forwards chunk
  imports to Metal, preserving no-copy buffers rather than duplicating guest RAM.
- A fixed 256 KiB event ring mirrors allocation generations, frees and hook readiness.
  Overflow or a generation mismatch permanently vetoes lifetime-based reclamation;
  missing history never authorizes resource removal.
- Before removing released texture metadata, a dedicated frontend worker revalidates
  its owner and holds the allocation lock until the backend completes removal. The
  protection reader remains independent, avoiding a protection/reclaim channel cycle.
- The initialization reply also synchronizes actual MetalFX availability to the game
  process; Vulkan-only providers remain hidden in its System menu.
- The ARM shader-resource-table walker is the existing portable bytecode interpreter.
  Its disk namespace is separate from x86 executable walker data; existing caches are
  preserved. The two-level GPU address table and shader addressing are unchanged.
- Velocity mirroring, motion-blur bypass, visibility queries, texture dimensionality
  checks (`0038`, `0039`, `0040`) and cache capacities retain their existing paths.
- `0085` carries hoisted compute dependencies through empty render passes to the next
  encoder. A deterministic ARM scheduler test exposed the missing dependency; the
  fix applies to both host architectures.
- Quickload remains offline. The optional `bbgpu_before_restart` ABI makes the Rosetta
  runtime stop and reap its owned ARM child before the next game starts.

The extra freed-storage experiment `BB_TEXTURE_RELEASED_STORAGE=1` is **not supported**
by this host yet: it also needs a cross-process virtual-mapping lease. Startup explicitly
rejects that combination. Its production default remains off; it is never silently enabled
or substituted with a smaller cache.

## Build

The engine and all changes are checked-in sources plus the ordered Metal patch series.
Initialize submodules and follow the normal source-build prerequisites first. Use separate
x86-64 and ARM64 dependency prefixes. ARM64 needs native fmt, Boost headers, xxhash,
SDL3, miniz, Zydis/Zycore, FFmpeg, Vulkan headers/loader, VMA headers, magic_enum,
robin-map and glslang, plus the same pinned SPIRV-Cross source as the x86 renderer.
Vulkan is a build dependency of shared code; this runtime renders directly through Metal.

Build SPIRV-Cross static `msl`, `glsl` and `core` libraries for ARM64 into the ARM prefix,
then build the two hosts separately:

```sh
BB_METAL_DEPS=/path/to/x86-prefix BB_BUILD_JOBS=1 \
  bash tools/macos/build_metal.sh
BB_METAL_DEPS=/path/to/x86-prefix BB_BUILD_JOBS=1 \
  bash tools/macos/build_metal_probe.sh
BB_METAL_ARM64_DEPS=/path/to/arm-prefix BB_BUILD_JOBS=1 \
  bash tools/macos/build_metal_arm64.sh
```

An optional `BB_METAL_HEADER_PREFIX` supplies additional header-only dependencies. The
ARM script can find native Homebrew packages. A release for macOS 26 must also use
runtime dependencies built for macOS 26: setting the project's deployment target does
not lower a prebuilt Homebrew library's minimum OS version.

The complete source-build entry now builds both architectures and supplies
`--arm64-build` and `--arm64-deps` to `package_metal.py`. A matching frontend, CPU probe,
ARM host and relocated dependency closure are required. `NATIVE-METAL.json` records
architecture, minimum OS and hashes; packaging checks every native dependency. A
separate game app bundle supplies the cover icon while the Rosetta frontend stays out
of the Dock. No game assets, credentials, test saves or development death tools belong
in a public package. The small bundled icon artworks retain their existing attribution.

## Validation

The transport test exercises ordering, overflow, stale generation rejection, cancellation
and an allocation racing with reclamation. It also supports real cross-architecture IPC:

```sh
cmake --build /path/to/arm-build --target remote-lifetime-test --parallel 1
cmake --build /path/to/x86-build --target remote-lifetime-test --parallel 1
/path/to/x86-build/remote-lifetime-test --cross-peer /path/to/arm-build/remote-lifetime-test
```

Native GPU tests cover two-level addressing, velocity batching, motion-blur bypass,
visibility queries, texture view ownership and 28 1D-to-2D shader variants. Gameplay QA
uses copied saves, settings and caches. Headless quicksave/quickload tokens call the
same runtime request as F2/F3; this verifies the recovery flow, not physical key delivery.
Death regression uses the existing development-only game-thread SetHp tool and proves
HP zero, loading and respawn; it does not prove enemy-combat coverage.

Performance comparison uses the same frontend, CPU runtime, scene and settings in ABBA
order, with only `BB_METAL_ARM64` changed. No GUI capture is used during scored windows.
The benchmark records both owned processes. Renderer footprint alone excludes the
Rosetta game in ARM mode; the sum of process ledgers is accounted usage, not a precise
measurement of unique physical RAM. System pressure, swap growth and owned Metal
allocations must also be compared. Normal saves, settings and caches are not modified.

## M4 comparison, 2026-10-23

Eight isolated fixed-camera runs used both ABBA and BAAB orders (four runs per mode),
45 seconds warmup and 45 seconds scoring each. Same frontend, CPU runtime, frozen
private save, cache seed and existing settings; only the host architecture changed.
Native 1080p, MetalFX off; resolution, effects and cache capacities were held constant.
This is one Central Yharnam standing scene, not a claim about all maps or 60 FPS.

| Metric | In-process x86 Metal | Separate ARM64 Metal |
| --- | ---: | ---: |
| Mean FPS | 44.64 | 47.27 (+5.9%) |
| Mean 1% low | 34.29 | 39.52 |
| Mean p95 frame time | 23.94 ms | 23.01 ms |
| Mean median accounted process footprint | 5788.7 MiB | 5519.0 MiB |
| Mean owned Metal allocation | 1192.2 MiB | 1154.3 MiB |

Accounted footprint includes both owned processes in ARM mode and can count shared
pages in both ledgers; it is not unique resident RAM. Scored swap was stable and system
pressure did not worsen. No computer-use capture ran during scored windows; external
screen observation was not independently verified. Startup/menu and icon integration
was completed after the eight-run measurement and receives separate boot/restart tests.

Transport tests passed in both directions across real x86/ARM processes. GPU tests cover
shared addressing, scheduler dependencies, velocity/blur bypass, visibility queries,
texture ownership, sampler memoization and 28 dimensional shader variants. Isolated
recovery and two consecutive death/respawn cycles passed. Recovery was driven through
the runtime input tokens rather than physical keyboard events; deaths used SetHp rather
than enemy combat. These are validation boundaries, not coverage of every gameplay path.
Networking routing/runtime tests passed; a fresh live-server login or multiplayer session
was not repeated for this architecture change. Networking remains in the game process.

### Owner's additional experimental combination

A final 30-second warmup / 45-second score pair also retained the owner's explicit
`BB_MTL_DEPTH_LAYOUT=1`, `BB_MTL_FILL_UPDATES=1` and
`BB_LIFETIME_HEAP_HISTORY_PRIORITY=1`. Native / fallback measured 50.62 / 45.94 FPS,
1% low 43.06 / 37.32. Accounted median process footprint was 5753.1 / 5701.0 MiB
(+52.1 MiB), and owned Metal allocations 1243.9 / 1083.7 MiB. Pressure was at the
same warning level in both; scored swap changed by -16 / 0 MiB. This pair confirms
startup and no pressure regression for that combination, not a repeatable percentage
gain or a universal memory reduction. Existing explicit options were retained.
The machine-readable [results](ARM64-METAL-RESULTS.json) keep it separate from the
eight-run aggregate. One earlier final-recovery attempt was stopped under critical
pressure while the browser was open and was excluded; the guarded retry passed.

## 0.2.51 follow-up

See [tessellation, binding ownership and native timing](METAL-TESSELLATION-AND-LIBRARY-REUSE.md). The architecture comparison above remains a 0.2.50 result. Library reuse is a separate default-OFF experiment.
