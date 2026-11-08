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

# 3. Assemble boot.s into bin/
nasm -f elf32 boot.s -o bin/boot.o

# 4. Compile the definitions module into bin/
$CC -m32 -c cykastd.c -o bin/cykastd.o -ffreestanding -O2 -nostdlib -Wall -Wextra

# 5. Compile kernel.c into bin/
$CC -m32 -c kernel.c -o bin/kernel.o -ffreestanding -O2 -nostdlib -Wall -Wextra

# 6. Link everything into bin/os_image.bin
$LD -m elf_i386 -T linker.ld bin/boot.o bin/cykastd.o bin/kernel.o -o bin/os_image.bin

echo "----------------------------------------"
echo "Build successful! Created bin/os_image.bin"
