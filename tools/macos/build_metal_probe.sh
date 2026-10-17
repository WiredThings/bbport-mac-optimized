#!/usr/bin/env bash
# Build only the Metal probe. The Vulkan fallback's probe remains unchanged.
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
repo=$PWD
prefix=${BB_METAL_DEPS:-$repo/deps-x86_64}
build=${BB_METAL_BUILD:-$repo/out/gpu-metal}
out=${BB_METAL_PROBE_OUT:-$repo/out/probe-metal}
mkdir -p "$out/atrac9"
for source in third_party/LibAtrac9/C/src/*.c; do
    clang -arch x86_64 -mmacosx-version-min=26.0 -std=c99 -O2 -w -c "$source" -o "$out/atrac9/$(basename "${source%.c}").o"
done
ar rcs "$out/libatrac9.a" "$out"/atrac9/*.o
clang -arch x86_64 -mmacosx-version-min=26.0 -std=c11 -O2 -g -Wall -Wextra -pthread \
    -I"$prefix/include" -I. -Isrc src/probe.c src/runtime*.c src/low_heap.c src/vulkan_smoke.c \
    "$out/libatrac9.a" -lm -L"$build" -lbbgpu -L"$prefix/lib" -lvulkan -lSDL3.0 \
    -Wl,-rpath,@loader_path/gpu-metal -Wl,-export_dynamic -o "$out/bb-probe-metal"
printf 'Built %s/bb-probe-metal (diagnostic snapshot ABI v1).\n' "$out"
