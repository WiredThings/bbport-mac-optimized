# Source for the standalone release

[User guide](../USER_GUIDE.md) · [中文使用指南](../USER_GUIDE.zh-CN.md) · [Third-party provenance](../THIRD_PARTY_CHANGES.md)

## Current M4 Metal work

The launcher, runtime integration, ordered Metal adapter patches and validation notes through
0.2.28 are on the fork's `optimized` branch:

```sh
git clone --recurse-submodules --branch optimized https://github.com/WiredThings/bbport-mac-optimized.git
```

Follow [METAL-BACKEND.md](METAL-BACKEND.md) for the pinned Metal engine and build settings.
The applied submodule worktrees are represented by the versioned ordered patch series;
`gpu/patches/metal` is part of this repository. The v0.2.28 tag and Release correspond to the current production launcher. See
[0.2.28.md](0.2.28.md) for the current changes and validation boundaries. The downloadable
source archive includes pinned submodule source with the adapter patches applied; private World Warper tooling,
recorded routes, local game data and saves are excluded. Use the recursive Git clone above
for building: unlike the inspection archive, it retains the Git metadata required by the
revision guards in the build scripts.

The published v0.2.28 build is pinned to commit
[`1186e6c955517fb1b82cee0fcde3b5c818102e69`](https://github.com/WiredThings/bbport-mac-optimized/commit/1186e6c955517fb1b82cee0fcde3b5c818102e69).
Subsequent documentation-only changes on `optimized` do not retag or change its App/source
assets or checksums. For the exact build snapshot, clone with `--branch v0.2.28`; for current
documentation, use `optimized`.

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

The packager also produces a local source export containing checked-out project and dependency sources. The public Release includes the app ZIP and a source archive with pinned submodule source. Source is also accessible through this tagged repository and the dependency links above. Component license notices are included in the app.
