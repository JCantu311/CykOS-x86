#!/bin/bash
set -e

mkdir -p bin

HOST_ARCH=$(uname -m)

# 1. Detect Cross-Compiler with ARM host protection
if command -v x86_64-linux-gnu-gcc &> /dev/null; then
    CC="x86_64-linux-gnu-gcc"
elif command -v x86_64-elf-gcc &> /dev/null; then
    CC="x86_64-elf-gcc"
elif command -v i686-elf-gcc &> /dev/null; then
    CC="i686-elf-gcc"
elif command -v gcc &> /dev/null; then
    if [[ "$HOST_ARCH" == "aarch64" || "$HOST_ARCH" == "arm64" || "$HOST_ARCH" == "armv7l" ]]; then
        echo "Error: Native 'gcc' on this machine outputs ARM code!" >&2
        echo "You are running on an ARM host ($HOST_ARCH) and need an x86 cross-compiler." >&2
        exit 1
    else
        CC="gcc"
    fi
else
    echo "Error: No suitable C compiler found!" >&2
    exit 1
fi

# 2. Detect Linker
if command -v ld.lld &> /dev/null; then
    LD="ld.lld"
elif command -v x86_64-linux-gnu-ld &> /dev/null; then
    LD="x86_64-linux-gnu-ld"
elif command -v x86_64-elf-ld &> /dev/null; then
    LD="x86_64-elf-ld"
elif command -v ld &> /dev/null; then
    LD="ld"
else
    echo "Error: No linker found!" >&2
    exit 1
fi

echo "Host Architecture: $HOST_ARCH"
echo "Using Compiler:    $CC"
echo "Using Linker:      $LD"
echo "----------------------------------------"

# Compile C files with absolute addressing guards and freestanding targets
gcc -m32 -ffreestanding -fno-pic -fno-pie -fno-stack-protector -c kernel.c -o kernel.o
gcc -m32 -ffreestanding -fno-pic -fno-pie -fno-stack-protector -c cykastd.c -o cykastd.o

# Assemble the boot sector
nasm -f elf32 boot.asm -o boot.o

# Link everything together into the final flat binary matching your script
ld -m elf_i386 -T linker.ld boot.o kernel.o cykastd.o -o os_image.bin

# NEW: Pad the file out cleanly to exactly 17 hardware sectors (512 * 17 = 8704 bytes)
truncate -s 8704 bin/os_image.bin

echo "----------------------------------------"
echo "Build successful! Standardized bin/os_image.bin to 8704 bytes."
