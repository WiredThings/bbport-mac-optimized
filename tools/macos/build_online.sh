#!/usr/bin/env bash
# Builds only the optional networking dylib. No game, settings or graphics cache is accessed.
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
deps=${BB_ONLINE_DEPS:-$PWD/deps-online-x86_64}
metal=${BB_METAL_DEPS:-$PWD/deps-x86_64}
headers=${BB_METAL_HEADER_PREFIX:-$metal}
build=${BB_ONLINE_BUILD:-$PWD/out/online}
jobs=${BB_BUILD_JOBS:-2}
cmake -S gpu/bbnet -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES=x86_64 -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0 \
    -DCMAKE_PREFIX_PATH="$deps;$metal;$headers;/opt/homebrew" \
    -DOPENSSL_ROOT_DIR="$deps" -DOPENSSL_USE_STATIC_LIBS=TRUE \
    -Dfmt_DIR="$metal/lib/cmake/fmt" \
    -DCMAKE_OSX_SYSROOT="${SDKROOT:-$(xcrun --sdk macosx --show-sdk-path)}"
cmake --build "$build" --target bbnet --parallel "$jobs"
printf 'Optional networking module: %s/libbbnet.dylib\n' "$build"
