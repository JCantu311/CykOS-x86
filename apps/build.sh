#!/bin/bash
set -e

# CykOS Rust Userland Application Build & Packaging Tool
# Builds standalone #![no_std] Rust apps, links them against kernel symbols,
# and updates the FAT16 filesystem image for QEMU.

cd "$(dirname "$0")/.."

mkdir -p apps/bin
mkdir -p bin

# 1. Detect Linker
if command -v i686-elf-ld &> /dev/null; then
    LD="i686-elf-ld"
elif command -v ld.lld &> /dev/null; then
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

# 2. Check rustc
if ! command -v rustc &> /dev/null; then
    echo "Error: rustc not found! Rust is required to build userland applications." >&2
    exit 1
fi

# Ensure i686 target is installed
if command -v rustup &> /dev/null; then
    rustup target add i686-unknown-linux-gnu 2>/dev/null || true
fi

# 3. Ensure Kernel Symbol Table exists for external linking
if [ ! -f bin/kernel_syms.ld ]; then
    if [ -f bin/kernel.map ]; then
        python3 -c "
import re
with open('bin/kernel.map') as f, open('bin/kernel_syms.ld', 'w') as out:
    for line in f:
        m = re.match(r'^\s+(0x[0-9a-fA-F]+)\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*$', line)
        if m:
            out.write(f'{m.group(2)} = {m.group(1)};\n')
" 2>/dev/null || true
    fi
fi

if [ ! -f bin/kernel_syms.ld ]; then
    echo "Warning: bin/kernel_syms.ld not found. Run ./make.sh first to build the kernel."
fi

# Function to compile a single Rust userland application
build_rust_app() {
    local src="$1"
    local name="$2"
    local upper_name=$(echo "$name" | tr '[:lower:]' '[:upper:]')
    local out_bin="apps/bin/${upper_name}.BIN"

    echo "Compiling Rust App: $src -> $out_bin"

    rustc --target i686-unknown-linux-gnu -O \
        -C overflow-checks=no \
        -C relocation-model=static \
        -C panic=abort \
        -C target-feature=-sse,-sse2 \
        --emit=obj "$src" -o "apps/bin/${name}.o"

    if [ -f bin/kernel_syms.ld ]; then
        $LD -m elf_i386 -T apps/apps.ld bin/kernel_syms.ld "apps/bin/${name}.o" -o "$out_bin"
    else
        $LD -m elf_i386 -T apps/apps.ld "apps/bin/${name}.o" -o "$out_bin"
    fi

    rm -f "apps/bin/${name}.o"
    echo "  [OK] Built $out_bin"
}

# 4. If a specific target file is passed as an argument, build only that
if [ -n "$1" ]; then
    target="$1"
    name=$(basename "$target" .rs)
    build_rust_app "$target" "$name"
else
    # Build root edit.rs if present
    if [ -f edit.rs ]; then
        build_rust_app "edit.rs" "edit"
        cp -f "apps/bin/EDIT.BIN" "bin/EDIT.BIN" 2>/dev/null || true
    fi

    # Build all apps/*.rs (excluding cykos.rs library)
    for src in apps/*.rs; do
        [ -e "$src" ] || continue
        name=$(basename "$src" .rs)
        [ "$name" = "cykos" ] && continue
        build_rust_app "$src" "$name"
    done
fi

# 5. Inject newly built apps into the QEMU hard disk image
if [ -f bin/os_image.bin ]; then
    echo "Updating FAT16 filesystem on bin/os_image.bin..."
    if command -v python3 &> /dev/null; then
        python3 mkfat16.py bin/os_image.bin
    elif command -v python &> /dev/null; then
        python mkfat16.py bin/os_image.bin
    fi
fi

echo "Done! Run QEMU to execute your userland apps."
