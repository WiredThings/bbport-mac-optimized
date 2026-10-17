#!/usr/bin/env bash
# Visual launcher's entry point. Metal runs in the Rosetta game process; the Vulkan
# compatibility option retains the native ARM64 renderer. Neither compiles at launch.
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
die() { echo "STOP: $*" >&2; exit 1; }
# Dedicated CLI entry only; normal Play always discards automation variables.
benchmark=0
if [[ ${1:-} == --benchmark ]]; then benchmark=1; shift; fi
renderer=${BB_RENDERER:-metal}
[[ $renderer == metal || $renderer == vulkan ]] || die "Unknown renderer: $renderer"
if [[ $renderer == metal ]]; then
    if [[ -f .packaged ]]; then
        probe=$PWD/bin/bb-probe-metal
        metal_lib=$PWD/bin/gpu-metal/libbbgpu.dylib
    else
        probe=$PWD/out/bb-probe-metal
        metal_lib=$PWD/out/gpu-metal/libbbgpu.dylib
    fi
    [[ -x $probe && -f $metal_lib ]] || die "The Metal renderer build is missing"
    export BB_PREBUILT=1 BB_NATIVE_GPU=0 BB_RENDERER=metal BB_PROBE=$probe
    export BB_PC_MODEL=1 BB_GUEST_IN_PLACE=1 BB_LAYER_MEMORY=1 BB_GUEST_MEMORY=host
    export BB_GPU_LIFETIME=${BB_GPU_LIFETIME:-1}
    export BB_PREUPLOAD=${BB_PREUPLOAD:-1} BB_UFFD=0 BB_COPY_GPU_BUFFERS=1
    export BB_GPU_WRITE_TWINS=1 BB_GPU_WRITE_TWINS_MAX=65536
    export BB_AUTO_NOHIZ=0 BB_GPU_LABELS=0 ROSETTA_ADVERTISE_AVX=1
    export BB_HDR=0
    unset BB_GPU_PROCESS BB_GPU_DEPS VK_DRIVER_FILES VK_ICD_FILENAMES
    unset BB_VULKAN_LIBRARY SDL_VULKAN_LIBRARY DYLD_LIBRARY_PATH BB_MAC_UNIFIED_MEMORY
else
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
fi
: "${BB_GAME_DIR:?Choose the game folder in the app}"
export BB_DATA_DIR=${BB_DATA_DIR:-$HOME/Library/Application Support/bbport/optimized-launcher}
export BB_USER_DIR=${BB_USER_DIR:-$HOME/Library/Application Support/bbport/user}
export BB_CONFIG=${BB_CONFIG:-$BB_DATA_DIR/bbport.ini}
if [[ $renderer == vulkan ]]; then
export BB_PREBUILT=1 BB_NATIVE_GPU=1 BB_PROBE=$probe BB_GPU_PROCESS=$gpu BB_GPU_DEPS=$deps
export BB_VULKAN_LIBRARY=$deps/lib/libvulkan.1.dylib SDL_VULKAN_LIBRARY=$deps/lib/libvulkan.1.dylib
export VK_DRIVER_FILES=$deps/lib/kosmickrisp/kosmickrisp_mesa_icd.json
unset VK_ICD_FILENAMES DYLD_LIBRARY_PATH
fi
# The precision policy changes generated SPIR-V. Keep the old release's caches intact,
# and separate the strict fallback from this port's optimized shader profile.
precision=stages
[[ ${BB_SPIRV_PRECISE:-} != all ]] || precision=all
if [[ $renderer == metal ]]; then
    export BB_GPU_USER_DIR=${BB_GPU_USER_DIR:-$BB_DATA_DIR/gpu-cache/metal1}
else
    export BB_GPU_USER_DIR=${BB_GPU_USER_DIR:-$BB_DATA_DIR/gpu-cache/perf1-$precision}
fi
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
export BB_PIPE_PARK_WAITS=0
[[ $renderer != vulkan ]] || export BB_MAC_UNIFIED_MEMORY=0
export BB_CMDBUF_TRIM=${BB_CMDBUF_TRIM:-1} BB_KK_POOL_FIX=${BB_KK_POOL_FIX:-1}
# These must never turn a normal Play click into an automated input/benchmark session.
if [[ $benchmark == 0 ]]; then
    unset BB_PAD_FILE BB_PAD_FILE_ONLY BB_PAD_REPLAY BB_PAD_RECORD BB_PRESET_FILE BB_TOGGLE_FILE
    unset BB_CAPTURE_NATIVE BB_CAPTURE_DIR
    unset BB_CAPTURE_TRIGGER BB_PRESENT_DUMP_TRIGGER BB_TIMEOUT BB_QUICKLOAD_PENDING
else
    [[ $renderer == metal && ${BB_PAD_FILE_ONLY:-} == 1 && -f ${BB_PAD_FILE:-} ]] || die "Benchmark requires Metal and isolated scripted input"
    [[ ${BB_QUICKSAVE:-0} == 0 ]] || die "Benchmark does not restore live quicksaves"
fi
mkdir -p "$BB_DATA_DIR/out"
export BB_FRAME_LOG=$BB_DATA_DIR/out/frames.csv
log=$BB_DATA_DIR/out/last-run.log
[[ ! -f $log ]] || cp "$log" "$BB_DATA_DIR/out/previous-run.log"
exec > >(tee "$log") 2>&1
echo "Bloodborne Optimized: renderer=$renderer; quicksave=$BB_QUICKSAVE"
echo "Runtime: $BB_PROBE"
echo "Renderer: ${metal_lib:-$BB_GPU_PROCESS}"
echo "Resolution patch: ${BB_PATCHES:-none (16:9)}"
echo "Game language: ${BB_LANGUAGE:-1} (Chinese also enables Unlock Game Region)"
if [[ $renderer == metal ]]; then
    echo "Metal: direct Metal API, Rosetta host, no-copy guest buffers; cache=metal1"
else
    echo "Renderer performance port 1: precision=$precision, compact-no-output=${BB_COMPACT_NO_OUTPUT:-1}"
    echo "Renderer memory fixes: command-trim=$BB_CMDBUF_TRIM, driver-pool-fix=$BB_KK_POOL_FIX"
fi
exec bash run.sh "$@"
