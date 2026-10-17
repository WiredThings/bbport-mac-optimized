# Source for the standalone release

## Current M4 Metal work

The launcher, runtime integration, ordered Metal adapter patches and validation notes through
0.2.16 are on the fork's `optimized` branch:

```sh
git clone --recurse-submodules --branch optimized https://github.com/WiredThings/bbport-mac-optimized.git
```

Follow [METAL-BACKEND.md](METAL-BACKEND.md) for the pinned Metal engine and build settings.
The applied submodule worktrees are represented by the versioned ordered patch series;
`gpu/patches/metal` is part of this repository. The latest local installation and app/source
archives do not by themselves create a GitHub Release. See
[METAL-ALPHA-MERGE.md](METAL-ALPHA-MERGE.md) for the current change and validation boundaries.

## Original standalone release (historical)

Application source, build scripts and dependency patches are published at the `v0.1.0` tag:

```sh
git clone --recurse-submodules --branch v0.1.0 https://github.com/DiAvisoo/bbport-mac-optimized.git
cd bbport-mac-optimized
```

Follow [BUILD-OPTIMIZED.md](BUILD-OPTIMIZED.md) to fetch the pinned dependencies and build the app. GitHub's automatic source ZIP does not contain submodules; use the recursive clone above.

- [Exact dependency revisions and repositories](0.1.0-dependencies.json).
- [Dependency downloads, revisions and build flags](../../tools/macos/setup_deps.sh).
- [FSR-Vulkan patches](../../gpu/patches/fsr-vulkan), applied by `tools/macos/build.sh`.
- [KosmicKrisp/Mesa patches](../../tools/macos/kosmickrisp-patches), applied by `tools/macos/setup_deps.sh`.
- [M2 renderer build flags](../../tools/macos/build_m2_native.command).
- [Packaging and source export](../../tools/macos/package_optimized.py).

The committed FSR-Vulkan and Mesa patches were checked against the exact local dependency trees used for the release binaries. All source modifications match; no additional private patches are required.

Python 3.11 is bundled with its license; the precise version appears in `THIRD-PARTY-NOTICES.txt` inside the app. CPython source is available from [python.org](https://www.python.org/downloads/source/). Boost, magic_enum and robin-map headers come from the Homebrew packages used by the build scripts.

The packager also produces a local source export containing checked-out project and dependency sources. Only the standalone app ZIP is uploaded as a release asset; source is accessible through this tagged repository and the pinned dependency links above. Component license notices are included in the app.
