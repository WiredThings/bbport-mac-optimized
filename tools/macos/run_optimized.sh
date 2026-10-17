#!/usr/bin/env bash
# Visual launcher's entry point. Uses the same native binaries and driver as the tested M2
# scripts, but receives display, save and advanced settings from the app. No installed release
# app dependency and no compilation at launch. Native release layout is documented below.
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
die() { echo "STOP: $*" >&2; exit 1; }
if [[ -f .packaged ]]; then
    # Self-contained release: x86-64 game runtime and native arm64 renderer.
    probe=$PWD/bin/bb-probe
    gpu=$PWD/bin/gpu-arm64/bb-gpu
    deps=$PWD/deps-arm64
else
    if [[ -f src/runtime_quicksave_bindings.h ]]; then default_native=$PWD; else default_native=$PWD/out/native-05-checkout; fi
    native=${BB_NATIVE_CHECKOUT:-$default_native}
    [[ -d $native ]] || die "Optimized native build is missing from $native"
    native=$(cd "$native" && pwd -P)
    probe=$native/out/bb-probe
    gpu=$native/out/gpu-arm64/bb-gpu
    deps=$native/deps-arm64
fi
[[ -x $probe && -x $gpu ]] || die "The optimized runtime or native renderer is missing"
[[ -f $(dirname "$probe")/gpu/libbbgpu.dylib && -f $(dirname "$gpu")/libbbgpu.dylib ]] || die "The optimized renderer libraries are missing"
[[ -f $deps/lib/libvulkan.1.dylib && -f $deps/lib/kosmickrisp/kosmickrisp_mesa_icd.json ]] || die "The native graphics driver is missing"
: "${BB_GAME_DIR:?Choose the game folder in the app}"
export BB_DATA_DIR=${BB_DATA_DIR:-$HOME/Library/Application Support/bbport/optimized-launcher}
export BB_USER_DIR=${BB_USER_DIR:-$HOME/Library/Application Support/bbport/user}
export BB_CONFIG=${BB_CONFIG:-$BB_DATA_DIR/bbport.ini}
export BB_PREBUILT=1 BB_NATIVE_GPU=1 BB_PROBE=$probe BB_GPU_PROCESS=$gpu BB_GPU_DEPS=$deps
export BB_VULKAN_LIBRARY=$deps/lib/libvulkan.1.dylib SDL_VULKAN_LIBRARY=$deps/lib/libvulkan.1.dylib
export VK_DRIVER_FILES=$deps/lib/kosmickrisp/kosmickrisp_mesa_icd.json
unset VK_ICD_FILENAMES DYLD_LIBRARY_PATH
# The precision policy changes generated SPIR-V. Keep the old release's caches intact,
# and separate the strict fallback from this port's optimized shader profile.
precision=stages
[[ ${BB_SPIRV_PRECISE:-} != all ]] || precision=all
export BB_GPU_USER_DIR=${BB_GPU_USER_DIR:-$BB_DATA_DIR/gpu-cache/perf1-$precision}
export MESA_SHADER_CACHE_DIR=$BB_DATA_DIR/mesa-cache-perf1 PYTHONDONTWRITEBYTECODE=1
if [[ -f .packaged ]]; then
    export PYTHON=$PWD/python/bin/python3 PYTHONHOME=$PWD/python PYTHONNOUSERSITE=1
    unset PYTHONPATH PYTHONSTARTUP
else
    export PYTHON=${PYTHON:-$(command -v python3)}
fi
export BB_QUICKSAVE=${BB_QUICKSAVE:-0}
export BB_QUICKSAVE_HELPER=$PWD/tools/macos/quicksave.py
export BB_QUICKSAVE_PYTHON=$PYTHON BB_QUICKSAVE_STATUS=$BB_DATA_DIR/out/quicksave-status.txt
export BB_SKIP_EMPTY_DRAWS=1 BB_VK_RECORD_THREADS=1 BB_FRAMES_AHEAD=2 BB_GPU_QOS=1
export BB_MAC_UNIFIED_MEMORY=0 BB_PIPE_PARK_WAITS=0
export BB_CMDBUF_TRIM=${BB_CMDBUF_TRIM:-1} BB_KK_POOL_FIX=${BB_KK_POOL_FIX:-1}
# These must never turn a normal Play click into an automated input/benchmark session.
unset BB_PAD_FILE BB_PAD_REPLAY BB_PAD_RECORD BB_PRESET_FILE BB_TOGGLE_FILE
unset BB_CAPTURE_TRIGGER BB_PRESENT_DUMP_TRIGGER BB_TIMEOUT BB_QUICKLOAD_PENDING
mkdir -p "$BB_DATA_DIR/out"
export BB_FRAME_LOG=$BB_DATA_DIR/out/frames.csv
log=$BB_DATA_DIR/out/last-run.log
[[ ! -f $log ]] || cp "$log" "$BB_DATA_DIR/out/previous-run.log"
exec > >(tee "$log") 2>&1
echo "Bloodborne Optimized: native Apple Silicon renderer; quicksave=$BB_QUICKSAVE"
echo "Runtime: $BB_PROBE"
echo "Renderer: $BB_GPU_PROCESS"
echo "Resolution patch: ${BB_PATCHES:-none (16:9)}"
echo "Game language: ${BB_LANGUAGE:-1} (Chinese also enables Unlock Game Region)"
echo "Renderer performance port 1: precision=$precision, compact-no-output=${BB_COMPACT_NO_OUTPUT:-1}"
echo "Renderer memory fixes: command-trim=$BB_CMDBUF_TRIM, driver-pool-fix=$BB_KK_POOL_FIX"
exec bash run.sh "$@"
