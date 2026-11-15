#!/bin/bash
set -e

# CykOS Rust Userland Application Build & Packaging Tool
# Builds standalone #![no_std] Rust apps as position-independent ELF objects.
# Kernel API imports are resolved by the runtime loader.

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

# 3. Build a single Rust userland application as a dynamic ELF object.
build_rust_app() {
    local src="$1"
    local name="$2"
    local upper_name=$(echo "$name" | tr '[:lower:]' '[:upper:]')
    local out_bin="apps/bin/${upper_name}.BIN"

    echo "Compiling Rust App: $src -> $out_bin"

    rustc --target i686-unknown-linux-gnu -O \
        -C overflow-checks=no \
        -C relocation-model=pic \
        -C panic=abort \
        -C target-feature=-sse,-sse2 \
        --emit=obj "$src" -o "apps/bin/${name}.o"

    $LD -m elf_i386 -shared -Bsymbolic --hash-style=sysv --gc-sections -e main -s \
        "apps/bin/${name}.o" -o "$out_bin"

    rm -f "apps/bin/${name}.o"
    echo "  [OK] Built $out_bin"
}

# 4. If a specific target file is passed as an argument, build only that
if [ -n "$1" ]; then
    target="$1"
    name=$(basename "$target" .rs)
    build_rust_app "$target" "$name"
else
    # Build the standalone editor from the organized user source tree.
    if [ -f src/user/edit.rs ]; then
        build_rust_app "src/user/edit.rs" "edit"
        cp -f "apps/bin/EDIT.BIN" "bin/EDIT.BIN" 2>/dev/null || true
    fi

    # Build all Rust apps below apps/ (excluding the SDK library).
    while IFS= read -r src; do
        name=$(basename "$src" .rs)
        [ "$name" = "cykos" ] && continue
        build_rust_app "$src" "$name"
    done < <(find apps -type f -name '*.rs' | sort)
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
