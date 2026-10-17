#!/usr/bin/env bash
# Build the pinned in-process Metal renderer. Does not start the game or replace an app.
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
repo=$PWD
source=$repo/engines/metal
prefix=${BB_METAL_DEPS:-$repo/deps-x86_64}
build=${BB_METAL_BUILD:-$repo/out/gpu-metal}
headers=${BB_METAL_HEADER_PREFIX:-$prefix}
jobs=${BB_BUILD_JOBS:-2}
[[ $jobs =~ ^[1-9][0-9]*$ ]] || { echo 'BB_BUILD_JOBS must be positive' >&2; exit 1; }
[[ $(git -C "$source" rev-parse HEAD) == 11992198df6306dacf6ec37de57ee920db1acc11 ]] || {
    echo 'Initialize engines/metal at the recorded submodule revision' >&2; exit 1;
}
apply_once() {
    local dir=$1 patch=$2
    if git -C "$dir" apply --reverse --check "$patch" 2>/dev/null; then return; fi
    git -C "$dir" apply --check "$patch"
    git -C "$dir" apply "$patch"
}
for patch in "$repo"/gpu/patches/metal/*.patch; do apply_once "$source" "$patch"; done
for patch in "$source"/gpu/patches/fsr-vulkan/*.patch; do
    apply_once "$source/gpu/third_party/fsr-vulkan" "$patch"
done
for name in msl glsl core; do
    [[ -f $prefix/lib/libspirv-cross-$name.a ]] || {
        echo 'SPIRV-Cross static libraries missing; see docs/release/METAL-BACKEND.md' >&2; exit 1;
    }
done
export MACOSX_DEPLOYMENT_TARGET=26.0
export PKG_CONFIG_LIBDIR="$prefix/lib/pkgconfig:$prefix/share/pkgconfig"
unset PKG_CONFIG_PATH
cmake -S "$source/gpu" -B "$build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=x86_64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0 -DCMAKE_PREFIX_PATH="$prefix;$headers;/opt/homebrew" \
    -Dfmt_DIR="$prefix/lib/cmake/fmt" -DZydis_DIR="$prefix/lib/cmake/zydis" \
    -DZycore_DIR="$prefix/lib/cmake/zycore" -Dminiz_DIR="$prefix/lib/cmake/miniz" \
    -DVulkan_LIBRARY="$prefix/lib/libvulkan.dylib" \
    -DSPIRV_CROSS_INCLUDE_DIR="$prefix/include" \
    -DSPIRV_CROSS_MSL_LIBRARY="$prefix/lib/libspirv-cross-msl.a" \
    -DSPIRV_CROSS_GLSL_LIBRARY="$prefix/lib/libspirv-cross-glsl.a" \
    -DSPIRV_CROSS_CORE_LIBRARY="$prefix/lib/libspirv-cross-core.a" \
    -DBB_LTO=ON -DBB_PGO=off -DBB_NET=OFF
cmake --build "$build" --target bbgpu --parallel "$jobs"
printf 'Built %s/libbbgpu.dylib; use package_metal.py to package it with the existing CPU runtime.\n' "$build"
