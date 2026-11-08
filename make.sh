#!/bin/bash
set -e # Stop the script if any command fails

# 0a. Locate the standard library source code
if ! RUST_SRC=$(rustc +nightly --print sysroot 2>/dev/null)/lib/rustlib/src/rust/library; then
    echo "Error: Could not find Rust sysroot. Make sure Rust Nightly is installed." >&2
    exit 1
fi

if [ ! -d "$RUST_SRC/core" ]; then
    rustup +nightly component add rust-src
fi

# 0b. Compile libcore and its compiler_builtins stubs simultaneously into an rlib
rustc +nightly --target i686-unknown-none.json \
  --edition 2024 \
  -Zunstable-options \
  -Zforce-unstable-if-unmarked \
  --cfg=bootstrap \
  --crate-type rlib \
  --crate-name core \
  "$RUST_SRC/core/src/lib.rs" \
  -O --out-dir .

# 1. Assemble the boot code from boot.s
nasm -f elf32 boot.s -o boot.o

# 2. Compile Rust source from kernel_main.rs
rustc +nightly --target i686-unknown-none.json \
  --edition 2024 \
  -Zunstable-options \
  --extern core=libcore.rlib \
  -C panic=abort \
  -O \
  --emit=obj \
  kernel_main.rs -o main.o

# 3. Link using LLVM's lld
ld.lld -m elf_i386 -T linker.ld boot.o main.o -o os_image.elf

# 4. Extract flat binary
rust-objcopy -O binary os_image.elf os_image.bin

echo "Build successful! Created os_image.bin"
