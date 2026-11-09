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

# 3. Assemble unified module
nasm -f elf32 boot.s -o bin/boot.o

# 4. Compile remaining C components (Keep your stable -O0 non-artifacting flags)
$CC -m32 -c cykastd.c -o bin/cykastd.o -ffreestanding -O0 -nostdlib -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fno-builtin -fno-stack-protector -Wall -Wextra
$CC -m32 -c kernel.c -o bin/kernel.o -ffreestanding -O0 -nostdlib -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fno-builtin -fno-stack-protector -Wall -Wextra

# 5. Link unified elements passing explicit non-PIE layout flags to the linker
$LD -m elf_i386 -T linker.ld --no-dynamic-linker -no-pie bin/boot.o bin/cykastd.o bin/kernel.o -o bin/os_image.bin

# 6. Pad binary out cleanly to 1MB (2048 sectors) to provide disk storage space beyond kernel
truncate -s 1048576 bin/os_image.bin

echo "----------------------------------------"
echo "Build successful! Standardized bin/os_image.bin to 1048576 bytes (1MB)."
