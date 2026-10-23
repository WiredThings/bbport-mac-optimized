#!/usr/bin/env bash
# Build only the experimental ARM64 Metal child; never launches a game or replaces an app.
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
repo=$PWD
source=$repo/engines/metal
prefix=${BB_METAL_ARM64_DEPS:-$repo/deps-arm64}
headers=${BB_METAL_HEADER_PREFIX:-$prefix}
build=${BB_METAL_ARM64_BUILD:-$repo/out/gpu-metal-arm64}
jobs=${BB_BUILD_JOBS:-1}
[[ $jobs =~ ^[1-9][0-9]*$ ]] || { echo 'BB_BUILD_JOBS must be positive' >&2; exit 1; }
[[ $(uname -s) == Darwin && $(uname -m) == arm64 ]] || { echo 'Run natively on Apple Silicon' >&2; exit 1; }
[[ $(git -C "$source" rev-parse HEAD) == 11992198df6306dacf6ec37de57ee920db1acc11 ]] || {
    echo 'Initialize engines/metal at the recorded submodule revision' >&2; exit 1;
}
for name in msl glsl core; do
    archive=$prefix/lib/libspirv-cross-$name.a
    [[ -f $archive && $(lipo -archs "$archive") == arm64 ]] || {
        echo "Expected an ARM64 SPIRV-Cross archive: $archive" >&2; exit 1;
    }
done
python3 "$repo/tools/macos/apply_metal_patches.py" "$source" "$repo"/gpu/patches/metal/*.patch
for patch in "$source"/gpu/patches/fsr-vulkan/*.patch; do
    if ! git -C "$source/gpu/third_party/fsr-vulkan" apply --reverse --check "$patch" 2>/dev/null; then
        git -C "$source/gpu/third_party/fsr-vulkan" apply --check "$patch"
        git -C "$source/gpu/third_party/fsr-vulkan" apply "$patch"
    fi
done
export MACOSX_DEPLOYMENT_TARGET=26.0
export PKG_CONFIG_LIBDIR="$prefix/lib/pkgconfig:$prefix/share/pkgconfig:/opt/homebrew/lib/pkgconfig:/opt/homebrew/share/pkgconfig"
unset PKG_CONFIG_PATH
cmake -S "$source/gpu" -B "$build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0 \
    -DCMAKE_OSX_SYSROOT="${SDKROOT:-$(xcrun --sdk macosx --show-sdk-path)}" \
    -DCMAKE_PREFIX_PATH="$prefix;$headers;/opt/homebrew" \
    -DSPIRV_CROSS_INCLUDE_DIR="$prefix/include" \
    -DSPIRV_CROSS_MSL_LIBRARY="$prefix/lib/libspirv-cross-msl.a" \
    -DSPIRV_CROSS_GLSL_LIBRARY="$prefix/lib/libspirv-cross-glsl.a" \
    -DSPIRV_CROSS_CORE_LIBRARY="$prefix/lib/libspirv-cross-core.a" \
    -DBB_LTO=ON -DBB_PGO=off -DBB_NET=OFF
cmake --build "$build" --target bb-gpu --parallel "$jobs"
printf 'Built %s/bb-gpu and libbbgpu.dylib. Read docs/release/ARM64-METAL-EXPERIMENT.md before staging.\n' "$build"
