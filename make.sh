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

# 4. Compile Level 1 Kernel components
$CC -m32 -c cykastd.c -o bin/cykastd.o -ffreestanding -O0 -nostdlib -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fno-builtin -fno-stack-protector -Wall -Wextra
$CC -m32 -c ata.c -o bin/ata.o -ffreestanding -O0 -nostdlib -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fno-builtin -fno-stack-protector -Wall -Wextra
$CC -m32 -c fat16.c -o bin/fat16.o -ffreestanding -O0 -nostdlib -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fno-builtin -fno-stack-protector -Wall -Wextra
$CC -m32 -c modload.c -o bin/modload.o -ffreestanding -O0 -nostdlib -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fno-builtin -fno-stack-protector -Wall -Wextra
$CC -m32 -c kernel.c -o bin/kernel.o -ffreestanding -O0 -nostdlib -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fno-builtin -fno-stack-protector -Wall -Wextra

# 5. Compile Level 2 Standalone Driver Modules
$CC -m32 -c keyboard.c -o bin/keyboard.o -ffreestanding -O0 -nostdlib -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fno-builtin -fno-stack-protector -Wall -Wextra
$LD -m elf_i386 -T module.ld --no-dynamic-linker -no-pie bin/keyboard.o -o bin/cykeybrd.drv

# 6. Link Level 1 Kernel
$LD -m elf_i386 -T linker.ld --no-dynamic-linker -no-pie bin/boot.o bin/cykastd.o bin/ata.o bin/fat16.o bin/modload.o bin/kernel.o -o bin/os_image.bin

# 7. Format FAT16 partition and package Level 2 drivers & configuration
if command -v python3 &> /dev/null; then
    python3 mkfat16.py bin/os_image.bin
elif command -v python &> /dev/null; then
    python mkfat16.py bin/os_image.bin
else
    truncate -s 16777216 bin/os_image.bin
fi

echo "----------------------------------------"
echo "Build successful! Created bin/os_image.bin with Level 1 Kernel & Level 2 Drivers."
