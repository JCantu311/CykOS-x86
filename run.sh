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
            # Probe using -device pcspk,audiodev=test without machine override
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

        if [[ ${#AUDIO_OPTS[@]} -eq 0 ]] && "$QEMU" -soundhw help 2>&1 | grep -qw 'pcspk'; then
            AUDIO_OPTS=(-soundhw pcspk)
        fi

        exec "$QEMU" -m 4G -drive file=bin/os_image.bin,format=raw,if=ide,index=0 "${AUDIO_OPTS[@]}"
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
