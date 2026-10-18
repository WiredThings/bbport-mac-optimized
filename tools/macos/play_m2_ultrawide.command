#!/usr/bin/env bash
# Interactive M2 Max profile: real 3840x1080, native renderer, normal controller input.
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
export BB_NATIVE_CHECKOUT=${BB_NATIVE_CHECKOUT:-$PWD/out/native-05-checkout}
export BB_DATA_DIR=${BB_DATA_DIR:-$PWD/out/m2-ultrawide}
mkdir -p "$BB_DATA_DIR/out"
if [[ ! -f $BB_DATA_DIR/bbport.ini ]]; then
    printf '%s\n' show_fps=1 model_lod=0 effect_dof=0 effect_motion_blur=0 \
        effect_dynamic_shadows=0 effect_game_aa=0 effect_chromatic_aberration=0 \
        effect_ssao=0 skip_intro=1 > "$BB_DATA_DIR/bbport.ini"
fi
# Automated controls must never take over an interactive play session.
unset BB_PAD_FILE BB_PAD_REPLAY BB_PAD_RECORD BB_PRESET_FILE BB_TOGGLE_FILE
unset BB_CAPTURE_TRIGGER BB_PRESENT_DUMP_TRIGGER BB_GPU_PROFILE BB_TIMEOUT
export BB_NATIVE_GPU=1 BB_ULTRAWIDE_UPSCALER=off BB_ULTRAWIDE_PRESET=0
export BB_SKIP_EMPTY_DRAWS=1 BB_VK_RECORD_THREADS=1 BB_FRAMES_AHEAD=2 BB_GPU_QOS=1
export BB_MAC_UNIFIED_MEMORY=0 BB_PIPE_PARK_WAITS=0
export BB_QUICKSAVE=${BB_QUICKSAVE:-1}
export BB_QUICKSAVE_HELPER=$PWD/tools/macos/quicksave.py
export BB_QUICKSAVE_PYTHON=$(command -v python3)
export BB_QUICKSAVE_STATUS=$BB_DATA_DIR/out/quicksave-status.txt
export BB_FULLSCREEN=${BB_FULLSCREEN:-1}
exec bash tools/macos/play_native_ultrawide.command "$@"
