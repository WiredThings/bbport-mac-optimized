#!/usr/bin/env bash
# 1920x1080 (16:9) FPS comparison using the working M2 native build and effect settings.
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
native=${BB_NATIVE_CHECKOUT:-$PWD/out/native-05-checkout}
[[ -x $native/out/bb-probe && -x $native/out/gpu-arm64/bb-gpu ]] || {
    echo "STOP: the native build is missing from $native" >&2
    exit 1
}
native=$(cd "$native" && pwd -P)
game=${BB_GAME_DIR:-$(defaults read io.github.bbport.mac gameFolder 2>/dev/null || true)}
[[ -f $game/eboot.bin ]] || { echo "STOP: set BB_GAME_DIR to the game folder" >&2; exit 1; }
export BB_DATA_DIR=${BB_DATA_DIR:-$PWD/out/m2-1080p}
mkdir -p "$BB_DATA_DIR/out" "$BB_DATA_DIR/patches" "$BB_DATA_DIR/mods"
export BB_CONFIG=$BB_DATA_DIR/bbport.ini
python3 - "$BB_CONFIG" "$PWD/out/m2-ultrawide/bbport.ini" <<'PY'
from pathlib import Path
import sys
target, source = map(Path, sys.argv[1:])
if target.exists():
    text = target.read_text()
elif source.exists():
    text = source.read_text()
else:
    text = ('model_lod=0\neffect_dof=0\neffect_motion_blur=0\neffect_dynamic_shadows=0\n'
            'effect_game_aa=0\neffect_chromatic_aberration=0\neffect_ssao=0\nskip_intro=1\n')
fixed = dict(output_res='1920x1080', upscaler='off', live_resolution='0', preset='0', show_fps='1')
lines = [line for line in text.splitlines() if line.partition('=')[0].strip() not in fixed]
target.write_text('\n'.join(lines + [f'{key}={value}' for key, value in fixed.items()]) + '\n')
PY
# No resolution/camera patch and no inherited render-size or scripted-input overrides.
unset BB_RENDER_RES BB_OUTPUT_RES BB_UI_RES BB_AUTO_RENDER_RES BB_DMEM_MB
unset BB_PAD_FILE BB_PAD_REPLAY BB_PAD_RECORD BB_PRESET_FILE BB_TOGGLE_FILE
unset BB_CAPTURE_TRIGGER BB_PRESENT_DUMP_TRIGGER BB_GPU_PROFILE BB_TIMEOUT
export BB_PATCHES=''
export BB_PATCHES_DIR=$BB_DATA_DIR/patches BB_PATCHES_CONFIG=$BB_DATA_DIR/patches.json
export BB_GAME_DIR=$game BB_USER_DIR=${BB_USER_DIR:-$HOME/Library/Application Support/bbport/user}
export BB_NATIVE_CHECKOUT=$native BB_PREBUILT=1 BB_NATIVE_GPU=1
export BB_PROBE=$native/out/bb-probe BB_GPU_PROCESS=$native/out/gpu-arm64/bb-gpu
export BB_GPU_DEPS=$native/deps-arm64
export BB_VULKAN_LIBRARY=$BB_GPU_DEPS/lib/libvulkan.1.dylib
export SDL_VULKAN_LIBRARY=$BB_VULKAN_LIBRARY
export VK_DRIVER_FILES=$BB_GPU_DEPS/lib/kosmickrisp/kosmickrisp_mesa_icd.json
export MESA_SHADER_CACHE_DIR=${MESA_SHADER_CACHE_DIR:-$BB_DATA_DIR/mesa-cache}
export PYTHONDONTWRITEBYTECODE=1
export BB_UPSCALER=off BB_LIVE_RES=0 BB_UPSCALE_PRESET=0
export BB_FPS=${BB_FPS:-uncap} BB_FULLSCREEN=${BB_FULLSCREEN:-0}
export BB_PRESENT_MODE=${BB_PRESENT_MODE:-Fifo}
export BB_SKIP_EMPTY_DRAWS=1 BB_VK_RECORD_THREADS=1 BB_FRAMES_AHEAD=2 BB_GPU_QOS=1
export BB_MAC_UNIFIED_MEMORY=0 BB_PIPE_PARK_WAITS=0
export BB_QUICKSAVE=${BB_QUICKSAVE:-1}
export BB_QUICKSAVE_HELPER=$PWD/tools/macos/quicksave.py
export BB_QUICKSAVE_PYTHON=$(command -v python3)
export BB_QUICKSAVE_STATUS=$BB_DATA_DIR/out/quicksave-status.txt
export BB_FRAME_STATS=1 BB_FRAME_LOG=$BB_DATA_DIR/out/frames.csv
log=$BB_DATA_DIR/out/last-run.log
[[ ! -f $log ]] || cp "$log" "$BB_DATA_DIR/out/previous-run.log"
exec > >(tee "$log") 2>&1
echo "Bloodborne FPS comparison: 1920x1080 (16:9), no resolution patch or upscaling"
echo "Settings and FPS log: $BB_DATA_DIR"
exec bash run.sh "$@"
