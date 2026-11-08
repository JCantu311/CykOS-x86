#!/bin/bash
nasm -f elf32 boot.asm -o boot.o
rustc --target i686-unknown-none -C panic=abort --emit=obj main.rs -o main.o
ld -m elf_i386 -T linker.ld boot.o main.o -o os_image.bin

