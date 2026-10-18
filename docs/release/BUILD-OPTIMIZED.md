# Build the current Metal + Vulkan app from source

[English quick start](../../README.md#build-from-source) · [中文快速开始](../../README.zh-CN.md#从源码构建)

## Source and prerequisites

Clone the public `optimized` branch recursively. The scripts require Git metadata and verify
pinned commits. The GitHub automatic ZIP excludes submodules; the Release source archive is
an inspection/export artifact, not a drop-in Git checkout. No game files, installed app,
previous binary release, private hooks, benchmark routes or proprietary toolchain package
is required to build. Playing still requires your own supported decrypted game.

Use Apple Silicon, macOS 26+, matching Xcode 27+/Command Line Tools with Swift and a macOS 27+
SDK, Rosetta 2, Apple Silicon Homebrew and network access. macOS SDK/frameworks are provided
by Apple, not redistributed here. Homebrew supplies native tools and header-only packages;
the remaining compiled dependencies are fetched at the commits in `setup_deps.sh` and
`setup_metal_deps.sh`. Homebrew/toolchain versions are not locked, so this is not a
bit-for-bit reproducible or offline build promise. An M4/16 GiB Mac should use one or two jobs,
with the game closed; reserve tens of GiB for two-architecture dependency/build/source trees.

## Complete build

```sh
git clone --recurse-submodules --branch optimized https://github.com/WiredThings/blood-echoes.git
cd blood-echoes
export SDKROOT="$(xcrun --sdk macosx --show-sdk-path)"
BB_BUILD_JOBS=2 bash tools/macos/build_from_source.sh 0.2.34-source1
```

The script performs the steps below in order. It builds its own Vulkan base package before
adding the newly built Metal runtime and renderer; `--baseline` never refers to a downloaded
or installed app. `--launcher` in this final step is that same source-built base.
No command installs the result or opens the game.

## Individual steps

Run from the repository root in a clean clone. `BB_BUILD_JOBS` also bounds the nested
Mesa/Ninja build. The x86 driver is unnecessary because Vulkan compatibility uses an arm64
GPU process, while the Metal process links only the x86 Vulkan loader.

```sh
export BB_BUILD_JOBS=2 BB_SWIFT_JOBS=2
export SDKROOT="$(xcrun --sdk macosx --show-sdk-path)"
git submodule update --init --recursive
BB_ARCH=x86_64 BB_SKIP_KOSMICKRISP=1 bash tools/macos/setup_deps.sh
BB_ARCH=arm64 bash tools/macos/setup_deps.sh
bash tools/macos/setup_metal_deps.sh
brew install python@3.11
export BB_PACKAGE_PYTHON="$(brew --prefix python@3.11)/Frameworks/Python.framework/Versions/3.11"

BB_SKIP_NATIVE_GPU=1 bash tools/macos/build.sh
bash tools/macos/build_m2_native.command
bash tools/macos/build_metal.sh
bash tools/macos/build_metal_probe.sh
swift test --package-path launcher-macos --jobs "$BB_SWIFT_JOBS"

python3 tools/macos/package_optimized.py 0.2.34-source1
python3 tools/macos/package_metal.py \
  --baseline "$PWD/out/package-optimized/blood-echoes-0.2.34-source1/Blood Echoes.app" \
  --launcher "$PWD/out/package-optimized/blood-echoes-0.2.34-source1/Blood Echoes.app" \
  --gpu-build "$PWD/out/gpu-metal" \
  --metal-probe "$PWD/out/probe-metal/bb-probe-metal" \
  --spirv-cross-source "$PWD/deps-x86_64/src/SPIRV-Cross" \
  --app "$PWD/out/source-release-0.2.34-source1/Blood Echoes.app" \
  --output "$PWD/dist" --version 0.2.34-source1 --verification-name source-build
```

`package_optimized.py` exports the current pinned and patched project/dependency sources,
licenses and a source manifest. Its preliminary ZIP is Vulkan-only; use the final
`Blood-Echoes-0.2.34-source1.zip` from `package_metal.py` for the combined app.
The source archive is `dist/blood-echoes-0.2.34-source1-source.tar.gz`.
The enhanced `bb-probe-metal` is rebuilt from the public root runtime source, rather than
preserved from a previous App. Both backends' runtime/renderer libraries are relocated into
the bundle and checked for non-system external loads. The icon and localization bundle are
source-controlled. Component source/licenses include SPIRV-Cross and the Metal adapter series.

## Verification and troubleshooting

- The build refuses uncommitted main-project source and an existing final staging directory.
  For a new final package use another suffix; dependency and compiled-object caches can stay.
- `BB_SKIP_DEPS=1` skips provisioning only after this workflow has prepared both prefixes.
  It does not select an installed app or import its binaries.
- Submodule patches make those worktrees appear dirty by design. Do not reset/clean them;
  the ordered Metal patch applicator recognizes a known applied prefix and resumes safely.
- Missing Python 3.11 framework: install `python@3.11`, or point `BB_PACKAGE_PYTHON` to that
  formula's `Frameworks/Python.framework/Versions/3.11`, not its `/bin` directory.
- SDK/linker errors: use a matching active toolchain (`xcode-select -p`, `clang --version`)
  and `SDKROOT`. The current engine names macOS 27 MetalFX properties inside runtime availability
  guards; these still need a 27+ SDK at compile time. The app deployment target stays 26.0.
- Dependency fetch errors stop the build; retry after resolving network access. Fixed revision
  mismatches stop rather than silently accepting a moved tag. External repositories/services
  must remain available; no project can guarantee every future network/toolchain combination.
- Packaging runs deep strict signature checks and eager-link, usage-only runtime checks with
  isolated paths. These exit before game/GPU initialization; they prove packaging/linkability,
  not gameplay visuals, performance, or identical bytes to the prebuilt Release.
- The 0.2.29 release/tag stays immutable. Its historical instructions used an existing base App;
  the `optimized` branch now provides the complete source bootstrap described here.

Earlier Vulkan lineage and changes are retained in Git history and the versioned technical notes.

## Source-build audit (2026-10-18)

A new public recursive clone was used to validate this workflow on an M4/16 GiB Mac
running macOS 27.0.1. All seven recursive submodule checkouts were obtained from GitHub;
the scripted runtime dependencies and driver were built from their public pinned sources.
The current Metal renderer was built with SDK 27. The existing installed App was not used
as a library source or package base.

The full entry point produced the combined Metal/Vulkan App ZIP and corresponding source
archive. A separately extracted copy passed deep strict codesign verification; all 98 Mach-O
files used only system or bundle-local dynamic loads. Bundled Python imports, both probes'
usage-only eager-link exits, and the native GPU process's no-argument eager-link exit passed.
36 Swift tests, 19 Python packaging/launch-path tests and the old-SDK early-rejection check
passed. Original preferences, 2,257 save/configuration files and installed app binaries were
unchanged. These are source/build/link tests; no game was started for this audit.
