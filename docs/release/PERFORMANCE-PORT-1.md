# Performance port 1 (0.1.3-perf1)

This is a source-level port of selected optimizations from Mark Hall's
[Wooloomooloo2/bloodborne_mac_port](https://github.com/Wooloomooloo2/bloodborne_mac_port).
It retains this fork's KosmicKrisp driver, ARM64 GPU process, SwiftUI launcher,
Chinese localization and automatic Chinese region-unlock patch.
The local preview is compiled with `-O3 -mcpu=apple-m4` and LTO for this M4 Mac mini; it is
not a universal release for earlier Apple Silicon generations.

## Included

- [5137a3e](https://github.com/Wooloomooloo2/bloodborne_mac_port/commit/5137a3ec646e0320205f868ea86972b2f45f185d):
  vertex-only passes with no color, depth or stencil attachments use a 16x16 render area,
  instead of walking a device-sized array of empty tiles. Vertex work is still executed.
  `BB_COMPACT_NO_OUTPUT=0` restores the previous render area for comparison.
- [fe06953](https://github.com/Wooloomooloo2/bloodborne_mac_port/commit/fe06953cebeba0cf116f7d73842623a902db5abe):
  Apple fragment/compute floating-point operations no longer carry SPIR-V `NoContraction`.
  Position-generating vertex, geometry and tessellation stages retain it, so passes agree
  on positions. `BB_SPIRV_PRECISE=all` restores the old precision policy. Linux retains
  the old default. The selected policy is part of the serialized shader profile.
- [29bd6c8](https://github.com/Wooloomooloo2/bloodborne_mac_port/commit/29bd6c822a24c4c0cd55ffce0dbf3f8d58d317ea):
  new shader permutations do not overwrite disk-cache indices that this session did not
  preload. Cached stages validate binding offsets. Null resources still reserve binding
  slots and cannot cause a shader with a different layout to be reused. Disk probing only
  occurs on a cache miss, not on every draw.

The launcher uses `gpu-cache/perf1-stages` (or `perf1-all` for strict precision) and
`mesa-cache-perf1` under its data directory. Existing shader caches are retained. The first
launch will compile a fresh cache; it is not a useful warm-cache performance comparison.

## Scope and limits

MoltenVK's Metal argument-buffer switch is not applicable to KosmicKrisp and is not set here.
The original upstream `NoContraction` gain was measured through SPIRV-Cross/MoltenVK;
KosmicKrisp uses a different shader compiler, so its gain on this backend is unmeasured.
Native ARM64 rendering, parallel startup preload and cache-profile persistence were already
present in this fork. Upload hoisting and upstream's background pipeline mode change queue
ownership/synchronization; they require a separate adaptation to this fork's recorder workers
and draw-preparation pipeline and are not included in this first port.

This work does not fix the 16GB Mac's resource-residency pressure by itself. No FPS gain or
gameplay correctness is claimed until the packaged engine is tested on the user's M4 with
the same save, location, settings and warmed caches. Offline shader tests do not establish
visual equivalence. Keep the previous installed app available for rollback.

## Offline verification

Build `bbgpu`, `bb-gpu` and `shader-performance-port-test` from `gpu/CMakeLists.txt`.
Run the shader test with an output directory and validate its six modules with `spirv-val`.
It checks actual SPIR-V emission in vertex/fragment/compute stages, the strict fallback,
cache-profile separation and the null-resource binding regression without launching a game
or creating a Vulkan device. Run `test_optimized_launcher.py` for cache-path isolation and
`test_patches.py` for Chinese region unlocking.

`tools/macos/package_performance_preview.py` overlays `--gpu-build` on a signed `--baseline`
0.1.2 app and writes a new `--app` and `--output` ZIP. It requires all dynamic dependencies
to retain the baseline's ABI/file names and macOS 26 minimum, preserves the driver/runtime,
relocates library paths, signs, audits and runs a usage-only dyld smoke test. Static miniz,
Zydis/Zycore and minimal FFmpeg use the pinned versions in `setup_deps.sh`. It never starts
the game, installs the app or changes user settings. The CPU runtime and launcher are not
recompiled; only the ARM64 renderer/host and launch scripts are replaced. Signing metadata
is refreshed when the new app is signed.

Adapted portions retain the shadPS4 GPL-2.0-or-later notices. Source commits above are by
Mark Hall and retain their upstream attribution. The resulting fork remains GPL-2.0-or-later.
