#!/usr/bin/env bash
# Add pinned Metal-only static dependencies to the x86_64 prefix; no app is required.
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
prefix=${BB_METAL_DEPS:-$PWD/deps-x86_64}
jobs=${BB_BUILD_JOBS:-2}
[[ $jobs =~ ^[1-9][0-9]*$ ]] || { echo 'BB_BUILD_JOBS must be positive' >&2; exit 1; }
[[ -f $prefix/env.sh ]] || { echo 'Run setup_deps.sh for x86_64 first.' >&2; exit 1; }
mkdir -p "$prefix/src"
fetch() {
    local name=$1 url=$2 revision=$3 dir=$prefix/src/$1
    if [[ ! -e $dir ]]; then
        mkdir -p "$dir"
        git -C "$dir" init -q
        git -C "$dir" fetch --depth 1 "$url" "$revision"
        git -C "$dir" -c advice.detachedHead=false checkout -q FETCH_HEAD
    fi
    [[ $(git -C "$dir" rev-parse HEAD) == "$revision" ]] || {
        echo "Unexpected revision at $dir; preserve it and use a fresh dependency prefix." >&2; exit 1;
    }
}
fetch SPIRV-Cross https://github.com/KhronosGroup/SPIRV-Cross.git f11ba9f0b21ba8fc15153d50a2a1ae31ab1cf8f7
cmake -S "$prefix/src/SPIRV-Cross" -B "$prefix/src/SPIRV-Cross/build-x86_64" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=x86_64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0 -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DBUILD_SHARED_LIBS=OFF -DSPIRV_CROSS_STATIC=ON -DSPIRV_CROSS_CLI=OFF \
    -DSPIRV_CROSS_ENABLE_TESTS=OFF
cmake --build "$prefix/src/SPIRV-Cross/build-x86_64" --parallel "$jobs"
cmake --install "$prefix/src/SPIRV-Cross/build-x86_64"
fetch xbyak-metal https://github.com/herumi/xbyak.git cd6bad3cad2e07a3e277c23d2b1f065320024717
# xbyak is header-only. Pin the newer Metal version for both consumers in this prefix.
mkdir -p "$prefix/include/xbyak"
cp "$prefix/src/xbyak-metal/xbyak/"*.h "$prefix/include/xbyak/"
printf 'Metal dependencies ready: %s\n' "$prefix"
