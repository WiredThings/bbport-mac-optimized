#!/usr/bin/env bash
# Complete source -> app build. No installed/released app, game or user configuration is read.
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
repo=$PWD
version=${1:-0.2.45-source1}
jobs=${BB_BUILD_JOBS:-2}
[[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[A-Za-z0-9.]+)?$ ]] || { echo 'Use a semantic version without v.' >&2; exit 1; }
[[ ! -e out/source-release-$version ]] || { echo 'Output already exists; choose a new version suffix.' >&2; exit 1; }
[[ $jobs =~ ^[1-9][0-9]*$ ]] || { echo 'BB_BUILD_JOBS must be positive' >&2; exit 1; }
[[ $(uname -s) == Darwin && $(uname -m) == arm64 ]] || { echo 'Use an Apple Silicon Mac.' >&2; exit 1; }
# Source packaging records a commit. Refuse to silently ship uncommitted/private source.
[[ -z $(git status --porcelain --untracked-files=normal --ignore-submodules=dirty) ]] || {
    echo 'Use a clean clone or commit your source before building a distributable app.' >&2; exit 1;
}
git submodule update --init --recursive
export BB_BUILD_JOBS=$jobs BB_SWIFT_JOBS=${BB_SWIFT_JOBS:-$jobs}
export SDKROOT=${SDKROOT:-$(xcrun --sdk macosx --show-sdk-path)}
# Runtime @available guards do not declare APIs absent from older SDK headers.
metal_header="$SDKROOT/System/Library/Frameworks/MetalFX.framework/Headers/MTLFXFrameInterpolator.h"
[[ -f $metal_header ]] && grep -q 'NSUInteger contentWidth' "$metal_header" || {
    echo 'Use a macOS 27+ SDK with its matching Xcode / Command Line Tools (SDKROOT).' >&2; exit 1;
}
if [[ ${BB_SKIP_DEPS:-0} != 1 ]]; then
    # Only the arm64 compatibility process needs KosmicKrisp.
    BB_ARCH=x86_64 BB_SKIP_KOSMICKRISP=1 bash tools/macos/setup_deps.sh
    BB_ARCH=arm64 bash tools/macos/setup_deps.sh
    bash tools/macos/setup_metal_deps.sh
    bash tools/macos/setup_online_deps.sh
    HOMEBREW_NO_INSTALL_UPGRADE=1 brew install python@3.11
fi
export BB_PACKAGE_PYTHON=${BB_PACKAGE_PYTHON:-$(brew --prefix python@3.11)/Frameworks/Python.framework/Versions/3.11}
# Build all shipped code from this checkout. Keep the Vulkan compatibility backend.
BB_SKIP_NATIVE_GPU=1 bash tools/macos/build.sh
bash tools/macos/build_m2_native.command
bash tools/macos/build_metal.sh
bash tools/macos/build_metal_probe.sh
bash tools/macos/build_online.sh
swift test --package-path launcher-macos --jobs "$BB_SWIFT_JOBS"
# First assemble a fresh Vulkan base FROM SOURCE, then add the rebuilt Metal runtime.
# This base is a disposable build artifact, never /Applications or a downloaded app.
python3 tools/macos/package_optimized.py "$version"
baseline="$repo/out/package-optimized/blood-echoes-$version/Blood Echoes.app"
python3 tools/macos/package_metal.py --baseline "$baseline" --launcher "$baseline" \
    --gpu-build "$repo/out/gpu-metal" --metal-probe "$repo/out/probe-metal/bb-probe-metal" \
    --online-build "$repo/out/online" --online-deps "${BB_ONLINE_DEPS:-$repo/deps-online-x86_64}" \
    --spirv-cross-source "$repo/deps-x86_64/src/SPIRV-Cross" \
    --app "$repo/out/source-release-$version/Blood Echoes.app" \
    --output "$repo/dist" --version "$version" --verification-name source-build
python3 - "$version" <<'PY_CHECKSUM'
import hashlib
from pathlib import Path
import sys
version = sys.argv[1]
files = [Path('dist') / f'Blood-Echoes-{version}.zip',
         Path('dist') / f'blood-echoes-{version}-source.tar.gz']
lines = []
for path in files:
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    lines.append(f'{digest.hexdigest()}  {path.name}\n')
(Path('dist') / 'source-build-SHA256SUMS.txt').write_text(''.join(lines))
PY_CHECKSUM
printf '\nComplete app: %s/out/source-release-%s/Blood Echoes.app\n' "$repo" "$version"
printf 'ZIP: %s/dist/Blood-Echoes-%s.zip\n' "$repo" "$version"
