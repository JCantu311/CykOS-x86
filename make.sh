#!/bin/bash
set -e

# 1. Install the gcc cross compiler package if you don't have it yet
# sudo dnf install gcc-x86_64-linux-gnu

# 2. Assemble your boot code
nasm -f elf32 boot.s -o boot.o

# 3. Compile your C kernel directly to a 32-bit object file
x86_64-linux-gnu-gcc -m32 -c kernel.c -o kernel.o -ffreestanding -O2 -nostdlib

# 4. Link everything into your final binary image using your existing script
ld.lld -m elf_i386 -T linker.ld boot.o kernel.o -o os_image.bin

echo "Build successful! Created os_image.bin"
