# Source for the standalone release

[User guide](../USER_GUIDE.md) · [中文使用指南](../USER_GUIDE.zh-CN.md) · [Third-party provenance](../THIRD_PARTY_CHANGES.md)

## Current M4 Metal work

Blood Echoes is the maintained WiredThings fork, formerly named Bloodborne Optimized.
Current launcher, runtime integration and ordered Metal adapter patches are published on
the `optimized` branch of `WiredThings/blood-echoes`:

```sh
git clone --recurse-submodules --branch optimized https://github.com/WiredThings/blood-echoes.git
cd blood-echoes
BB_BUILD_JOBS=2 bash tools/macos/build_from_source.sh 0.2.36-source1
```

See [BUILD-OPTIMIZED.md](BUILD-OPTIMIZED.md) and the [README](../../README.md#build-from-source).
The build creates its own base app and needs no installed app or game files. The complete
Release source archive contains pinned submodule sources with all adapter patches applied.
Use the recursive clone to retain the Git metadata required by revision guards.

The `v0.2.36` tag is the archive-safety build snapshot; older tags and their asset filenames
remain historical releases. Private World Warper tools, routes, game data and saves are excluded.

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
