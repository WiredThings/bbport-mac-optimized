#!/usr/bin/env bash
# Rebuild an already prepared macos-native checkout for this M2 Mac.
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
native=${BB_NATIVE_CHECKOUT:-$PWD}
native=$(cd "$native" && pwd -P)
deps=$native/deps-arm64
[[ -f $deps/env.sh ]] || { echo "STOP: prepare the native checkout's arm64 dependencies first" >&2; exit 1; }
source "$deps/env.sh"
# The installed linker cannot read the default macOS 27 SDK's arm64e.x1 stubs.
sdk=${SDKROOT:-/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk}
[[ -d $sdk ]] || sdk=$(xcrun --sdk macosx --show-sdk-path)
export SDKROOT=$sdk
cmake -S "$native/gpu" -B "$native/out/gpu-arm64" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_SYSTEM_NAME=Darwin -DCMAKE_SYSTEM_PROCESSOR=arm64 \
    -DCMAKE_OSX_SYSROOT="$sdk" -DCMAKE_OSX_DEPLOYMENT_TARGET="$MACOSX_DEPLOYMENT_TARGET" \
    -DCMAKE_PREFIX_PATH="$deps;/opt/homebrew" -DBB_LTO=ON -DBB_PGO=off \
    -DCMAKE_C_FLAGS_RELEASE='-O3 -DNDEBUG -mcpu=apple-m2' \
    -DCMAKE_CXX_FLAGS_RELEASE='-O3 -DNDEBUG -mcpu=apple-m2' \
    -DCMAKE_OBJCXX_FLAGS_RELEASE='-O3 -DNDEBUG -mcpu=apple-m2'
jobs=${BB_BUILD_JOBS:-2}
[[ $jobs =~ ^[1-9][0-9]*$ ]] || { echo 'BB_BUILD_JOBS must be positive' >&2; exit 1; }
ninja -C "$native/out/gpu-arm64" -j "$jobs" bbgpu bb-gpu
