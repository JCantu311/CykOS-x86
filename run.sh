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
        exec "$QEMU" -drive file=bin/os_image.bin,format=raw,if=ide,index=0
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
