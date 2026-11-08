#!/bin/bash
set -e # Stop the script if any command fails

# 1. Assemble the boot code to a 32-bit x86 ELF object
nasm -f elf32 boot.s -o boot.o

# 2. Compile Rust source targeting 32-bit bare-metal x86
rustc --target i686-unknown-none -C panic=abort --emit=obj kernel_main.rs -o main.o

# 3. Link using LLVM's lld (explicitly stating x86 emulation)
ld.lld -m elf_i386 -T linker.ld boot.o main.o -o os_image.bin

echo "Build successful! Created os_image.bin"
