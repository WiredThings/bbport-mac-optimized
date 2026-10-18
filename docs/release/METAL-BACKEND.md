# Metal backend, 0.2.1-metal-mem1 (historical)

For the current complete source build, use [BUILD-OPTIMIZED.md](BUILD-OPTIMIZED.md).
The baseline-overlay example below records the original integration, not the current bootstrap.

The memory-accounting follow-up is described in METAL-MEMORY-1.md.

The existing Bloodborne Optimized app now defaults to Huax-1839/bloodborne-macos
11992198df6306dacf6ec37de57ee920db1acc11. The renderer calls Metal directly; no
MoltenVK or KosmicKrisp device is used on this path. Its host code is x86_64 under
Rosetta in the game process. This is not an ARM64-native game or renderer process.

The launcher offers Vulkan (compatibility) in the same app. That retains the exact
0.1.4-mem1 ARM64 renderer, driver and CPU runtime. Existing save locations and game
folders are unchanged. Metal gets its own gpu-cache/metal1 directory; existing
Vulkan pipeline/driver caches are preserved and are not imported into Metal.

## Compatibility

| Existing feature | Metal integration |
| --- | --- |
| Simplified/traditional Chinese launcher | Preserved |
| Game language, Unlock Game Region | Original patch compiler and language IDs preserved |
| FPS, resolution and direct-memory patches | Original compiler retained; MetalFX presets use the same scene-size patch |
| Save data, controller mappings, quicksave hooks | Exact CPU runtime instructions preserved; three graphics ABI adapters added |
| F1/custom settings shortcut | Adapter reuses the existing shortcut header |
| FSR3/TAA | Converted to MetalFX, preserving preset; Off stays Off |
| Camera motion vectors | Supported by upstream Metal path |
| Object motion vectors, RCAS, HDR | Unavailable; disabled in launcher and excluded from saved launch settings |
| Ultrawide community patches | Existing native-resolution/Off behavior retained; gameplay not verified |
| Compact Vulkan no-output shader, selective SPIR-V precision | Remain in Vulkan fallback; Metal uses its own shader translation and math policy |
| Command pool trim, KK pool fix | Remain in Vulkan fallback; do not apply to Metal resources |
| Graphics ledger budget | Adapted to Metal in 0.2.1; see METAL-MEMORY-1.md |
| Asset/game mods | Existing mod preparation retained; arbitrary third-party mods need gameplay verification |

These are source, configuration and interface compatibility results. Gameplay,
quicksave restoration, visual correctness and FPS on this exact packaged build
still require a user-run session. The integration tests never start the game.
Neither lower total memory use nor a particular FPS is guaranteed.

## Integration

The engines/metal submodule pins the full source. gpu/patches/metal contains the
small adapter series: main-thread window creation for the existing CPU executable,
same-process memory sharing, keyboard state, and the existing menu shortcut.
The original CPU binary is copied to bb-probe-metal; only its libbbgpu dependency
and ad-hoc signature change. Packaging compares its __TEXT,__text bytes to the
baseline. All 33 runtime imports resolve to the preserved executable. Vulkan's
bb-probe and both original renderer libraries stay intact.

## Build

Use Command Line Tools, Rosetta, CMake/Ninja, glslang, pkgconf and the header-only
Boost, magic_enum and robin-map dependencies documented in BUILD-OPTIMIZED.md.
Initialize the pinned submodules recursively. The renderer requires x86_64 SDL3,
FFmpeg, fmt, xxHash, Vulkan Loader, static miniz and Zydis/Zycore. They can be built
with tools/macos/setup_deps.sh (BB_ARCH=x86_64); the Vulkan loader is only a link
dependency on the Metal path. Do not link ARM64 archives into this renderer.

Additional dependency pins for this build:

- SPIRV-Cross f11ba9f0b21ba8fc15153d50a2a1ae31ab1cf8f7, tag vulkan-sdk-1.4.363.0
- xbyak cd6bad3cad2e07a3e277c23d2b1f065320024717, tag v7.43 (headers)
- miniz 77d0dce8627735138c51770d1799a1ef48f2117d
- Zydis a2278f1d254e492f6a6b39f6cb5d1f5d515659dc
- Zycore 0b2432ced0884fd152b471d97ecf0258ff4d859f

Build SPIRV-Cross from its pinned source with CMAKE_OSX_ARCHITECTURES=x86_64,
CMAKE_OSX_DEPLOYMENT_TARGET=26.0, BUILD_SHARED_LIBS=OFF, SPIRV_CROSS_STATIC=ON,
SPIRV_CROSS_CLI=OFF, SPIRV_CROSS_ENABLE_TESTS=OFF and install into the same prefix.
Copy the pinned xbyak/xbyak headers to that prefix's include/xbyak.

```
BB_METAL_DEPS=/path/to/x86-prefix BB_BUILD_JOBS=2 bash tools/macos/build_metal.sh
BB_SWIFT_JOBS=2 BB_RELEASE=0.2.0-metal1 BB_APP=/tmp/metal-launcher.app bash tools/macos/build_launcher.sh
python3 tools/macos/package_metal.py --baseline '/path/to/0.1.4-mem1/Bloodborne Optimized.app' --gpu-build out/gpu-metal --launcher /tmp/metal-launcher.app --spirv-cross-source /path/to/SPIRV-Cross --app '/tmp/metal-package/Bloodborne Optimized.app' --output dist
```

Packaging requires a fresh stage directory, checks baseline signatures, relocates
all new library references to bundled dependencies, signs and verifies the app,
and runs eager dyld binding with no game arguments. Nothing installs automatically.
Use two compilation jobs on the 16 GB Mac and exit the game before building.

## Verification

The current M4 verification passed 34 host shader compilations (26 compute
pipelines), Metal scheduler, runtime texture/buffer/guest-memory tests, reduced
scene targets, and pipeline profile/serialization tests. Launcher localization,
Metal/Vulkan environment selection, Chinese region unlock, resolution/FPS patch
compilation and quicksave file operations are tested separately. See the local
metal-backend-verification.json and test logs for measured results.

To revert, choose Vulkan (compatibility) in Display settings. The complete
0.1.4-mem1 app archive remains a separate rollback option; no save conversion is
performed. Do not clear saves or old caches to switch backends.
