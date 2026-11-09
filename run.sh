#!/bin/bash
if command -v qemu-system-i386 &> /dev/null; then
    QEMU="qemu-system-i386"
elif command -v qemu-system-x86_64 &> /dev/null; then
    QEMU="qemu-system-x86_64"
else
    echo "Error: qemu-system-i386 or qemu-system-x86_64 not found!" >&2
    exit 1
fi

$QEMU -drive file=bin/os_image.bin,format=raw,bus=0,unit=0,media=disk
