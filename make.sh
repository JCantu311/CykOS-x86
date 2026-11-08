#!/bin/bash
set -e

# Create the bin directory if it doesn't exist
mkdir -p bin

# 1. Detect Cross-Compiler
if command -v x86_64-linux-gnu-gcc &> /dev/null; then
    CC="x86_64-linux-gnu-gcc"
elif command -v x86_64-elf-gcc &> /dev/null; then
    CC="x86_64-elf-gcc"
elif command -v gcc &> /dev/null; then
    CC="gcc"
else
    echo "Error: No suitable C compiler (gcc) found!" >&2
    exit 1
fi

# 2. Detect Linker
if command -v ld.lld &> /dev/null; then
    LD="ld.lld"
elif command -v ld &> /dev/null; then
    LD="ld"
else
    echo "Error: No linker found!" >&2
    exit 1
fi

echo "Using Compiler: $CC"
echo "Using Linker:   $LD"

# 3. Assemble boot.s into bin/
nasm -f elf32 boot.s -o bin/boot.o

# 4. Compile kernel.c into bin/
$CC -m32 -c kernel.c -o bin/kernel.o -ffreestanding -O2 -nostdlib -Wall -Wextra

# 5. Link everything into bin/os_image.bin
$LD -m elf_i386 -T linker.ld bin/boot.o bin/kernel.o -o bin/os_image.bin

echo "----------------------------------------"
echo "Build successful! Created bin/os_image.bin"
