#!/bin/sh
# INPUT:  ARM64 Linux binaries, Linux-rewritten modules, matching shader cache/RomFS.
# OUTPUT: One isolated attempt and log; fresh saves unless explicitly selected.
# POS:    Device bring-up; persistent save/profile paths require explicit selection.
set -eu
umask 077
root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
game=${1:?Usage: run-bringup.sh GAME_ROOT [graphics|surface-probe|display-probe|audio-probe|cpu-mode]}
game=$(CDPATH= cd -- "$game" && pwd)
mode=${2:-graphics}
patches=${D2R_PATCH_DIR:-$game/linux-native}
case "$mode" in
    surface-probe|display-probe|audio-probe) ;;
    *)
    for name in main sdk subsdk0; do
        test -f "$patches/$name.patched.bin"
    done
    ;;
esac
case "$mode" in graphics|game) test -f "$game/Update/RomFS/data/build_manifest.psv" ;; esac
diagnostics=${D2R_DIAGNOSTICS:-0}
case "$diagnostics" in 0|1) ;; *) echo 'D2R_DIAGNOSTICS requires 0 or 1' >&2; exit 2;; esac
if [ "$diagnostics" = 1 ]; then
    state=${D2R_STATE_DIR:-$root/runs}
    mkdir -p "$state"
    run=$(mktemp -d "$state/run.XXXXXX")
else
    run=$(mktemp -d "${TMPDIR:-/tmp}/d2r-session.XXXXXX")
fi
mkdir "$run/save" "$run/temp" "$run/profile"
echo "$$" > "$run/pid"
save=${D2R_SAVE_DIR:-$run/save}
profile=${D2R_PROFILE_DIR:-$run/profile}
mkdir -p "$save" "$profile"
export DM_ROMFS="$game/Update/RomFS" DM_SAVE="$save" DM_TEMP="$run/temp"
export DM_PROFILE_DIR="$profile"
export DM_OPMODE=0 DM_APP_POOL_ADD_MB=0 DM_SHADER_IDENTITY_CODE=1
export DM_LANG="${DM_LANG:-zh-Hans}"
unset DM_DBG DM_SVC DM_MEMORY_REPORT DM_FRAME_DIR DM_CAPTURE_FRAMES
if [ "$diagnostics" = 1 ]; then
    export DM_DBG=1 DM_SVC=1 DM_MEMORY_REPORT="$run/memory.json" DM_FRAME_DIR="$run/frames"
    mkdir "$run/frames"
    if [ "${D2R_CAPTURE_FRAMES:-0}" != 0 ]; then export DM_CAPTURE_FRAMES="$D2R_CAPTURE_FRAMES"; fi
fi
case "$mode" in
    graphics|surface-probe|display-probe)
        export DM_SHADER_ROOT="${DM_SHADER_ROOT:-$game/shaders-runtime-r4}"
        if [ "$mode" = graphics ] && [ ! -f "$DM_SHADER_ROOT/shader_index.bin" ]; then
            image=${DM_SHADER_IMAGE:-$game/Downloads/shaders-runtime-r4.squashfs}
            test -f "$image"
            mkdir -p "$DM_SHADER_ROOT"
            if [ -n "$(ls -A "$DM_SHADER_ROOT")" ]; then
                echo 'Refusing to mount over a nonempty shader directory' >&2
                exit 1
            fi
            mount -t squashfs -o ro,loop "$image" "$DM_SHADER_ROOT"
            test -f "$DM_SHADER_ROOT/shader_index.bin"
        fi
        if [ "${DM_WAYLAND_WSI_DIR+x}" != x ] && [ -f "$root/wayland-wsi/VkLayer_window_system_integration.json" ]; then
            export DM_WAYLAND_WSI_DIR="$root/wayland-wsi"
        fi
        if [ -n "${DM_WAYLAND_WSI_DIR:-}" ]; then
            export SDL_VIDEODRIVER=wayland
            export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run}" WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"
        fi
        binary="$root/d2r-linux"
        if [ "${D2R_SKIP_MOVIES:-0}" = 1 ]; then
            binary="$root/d2r-linux-novideo"
            export DM_MOVIE=1
        fi
        [ "$mode" != graphics ] || mode=game
        ;;
    audio-probe) binary="$root/d2r-linux" ;;
    load|os-init|os-services|sdk-startup|game) binary="$root/d2r-linux-host-probe" ;;
    *) echo 'Unknown bring-up mode' >&2; exit 2 ;;
esac
if [ "$diagnostics" = 1 ]; then
    echo "Linux bring-up log: $run/stdout.log"
    exec >"$run/stdout.log" 2>&1
fi
case "$mode" in
    surface-probe|display-probe|audio-probe)
        exec "$binary" "--$mode" "$run/result.json"
        ;;
esac
exec "$binary" "$patches" "$run/result.json" "$mode"
