#!/bin/bash
set -e

MODE="${1:-qemu}"
case "$MODE" in
    qemu)
        if command -v qemu-system-i386 &> /dev/null; then
            QEMU="qemu-system-i386"
        elif command -v qemu-system-x86_64 &> /dev/null; then
            QEMU="qemu-system-x86_64"
        else
            echo "Error: QEMU is not installed." >&2
            exit 1
        fi
        # Detect and test working audio backend for WSL / Linux (PulseAudio, ALSA, OSS, or SDL)
        AUDIO_OPTS=()
        test_audio_driver() {
            local drv="$1"
            "$QEMU" -audiodev help 2>&1 | grep -qw "$drv" || return 1
            "$QEMU" -audiodev "id=test,driver=$drv" -device pcspk,audiodev=test -display none -serial none -monitor none </dev/null &> /dev/null
        }

        # Check WSLg PulseAudio socket first
        if [[ -e "/mnt/wslg/runtime-dir/pulse/native" ]] && [[ -z "${PULSE_SERVER:-}" ]]; then
            export PULSE_SERVER="unix:/mnt/wslg/runtime-dir/pulse/native"
        fi

        for drv in pa alsa sdl oss dsound default; do
            if test_audio_driver "$drv"; then
                AUDIO_OPTS=(-audiodev "id=snd0,driver=${drv}" -device "pcspk,audiodev=snd0")
                break
            fi
        done

        # Fallback 1: try bare -device pcspk (QEMU may pick a default audio backend)
        if [[ ${#AUDIO_OPTS[@]} -eq 0 ]]; then
            "$QEMU" -device pcspk -display none -serial none -monitor none </dev/null &> /dev/null && \
                AUDIO_OPTS=(-device pcspk)
        fi

        # Fallback 2: legacy -soundhw pcspk (older QEMU)
        if [[ ${#AUDIO_OPTS[@]} -eq 0 ]] && "$QEMU" -soundhw help 2>&1 | grep -qw 'pcspk'; then
            AUDIO_OPTS=(-soundhw pcspk)
        fi

        # Try different PC speaker device names for QEMU compatibility.
        # QEMU 11.x may use 'isa-pcspk' instead of 'pcspk'; older builds may not have pcspk at all.
        PCSPEAKER_DEVICES=("pcspk" "isa-pcspk")
        AUDIO_OPTS=()
        FOUND_AUDIO=0

        for drv in pa alsa sdl oss pipewire jack default; do
            for pcm in "${PCSPEAKER_DEVICES[@]}"; do
                test_opts=(-audiodev "id=snd0,driver=${drv}" -device "${pcm},audiodev=snd0")
                if "$QEMU" "${test_opts[@]}" -display none -serial none -monitor none </dev/null &> /dev/null; then
                    AUDIO_OPTS=("${test_opts[@]}")
                    FOUND_AUDIO=1
                    break 2
                fi
            done
        done

        if [[ $FOUND_AUDIO -eq 0 ]]; then
            # Fallback: try bare -device without audiodev (QEMU may pick default backend)
            for pcm in "${PCSPEAKER_DEVICES[@]}"; do
                if "$QEMU" -device "$pcm" -display none -serial none -monitor none </dev/null &> /dev/null; then
                    AUDIO_OPTS=(-device "$pcm")
                    FOUND_AUDIO=1
                    break
                fi
            done
        fi

        # A manual override takes precedence over automatic backend detection.
        if [[ -n "${CYKOS_QEMU_AUDIO:-}" ]]; then
            # shellcheck disable=SC2206
            AUDIO_OPTS=(${CYKOS_QEMU_AUDIO})
            FOUND_AUDIO=1
        fi

        if [[ $FOUND_AUDIO -eq 1 ]]; then
            echo "[run.sh] QEMU audio: ${AUDIO_OPTS[*]}"
            exec "$QEMU" -m 4G -vga std -drive file=bin/images/cykos.img,format=raw,if=ide,index=0 "${AUDIO_OPTS[@]}"
        fi

        echo "[run.sh] Warning: No PC speaker device/audio backend combination works with this QEMU build."
        echo "[run.sh] Sound will be silent in QEMU."
        echo "[run.sh] Try: CYKOS_QEMU_AUDIO='-audiodev id=snd0,driver=alsa' $0 qemu"
        echo "[run.sh] Or check: qemu-system-i386 -device help | grep -i -E 'pcspk|speaker'"

        # Allow manual override via CYKOS_QEMU_AUDIO env var
        if [[ -n "${CYKOS_QEMU_AUDIO:-}" ]]; then
            # shellcheck disable=SC2086
            AUDIO_OPTS=(${CYKOS_QEMU_AUDIO})
            echo "[run.sh] Using override: ${AUDIO_OPTS[*]}"
            exec "$QEMU" -m 4G -vga std -drive file=bin/images/cykos.img,format=raw,if=ide,index=0 "${AUDIO_OPTS[@]}"
        fi

        exec "$QEMU" -m 4G -vga std -drive file=bin/images/cykos.img,format=raw,if=ide,index=0
        ;;
    virtualbox)
        if [[ $# -ne 2 ]] || ! command -v VBoxManage &> /dev/null; then
            echo "Usage: $0 virtualbox <registered-vm-name>" >&2
            exit 1
        fi
        exec VBoxManage startvm "$2"
        ;;
    vmware)
        if [[ $# -ne 2 ]] || ! command -v vmrun &> /dev/null; then
            echo "Usage: $0 vmware <path-to-vmx>" >&2
            exit 1
        fi
        exec vmrun start "$2" nogui
        ;;
    *)
        echo "Usage: $0 [qemu|virtualbox <registered-vm-name>|vmware <path-to-vmx>]" >&2
        exit 1
        ;;
esac
