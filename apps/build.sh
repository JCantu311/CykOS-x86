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

# 3. Build a Rust application according to APP_LINKING.LST (or environment).
app_link_mode() {
    local upper_name="$1"
    local fallback="${2:-static}"
    local manifest_mode=""
    if [ -f APP_LINKING.LST ]; then
        manifest_mode=$(awk -v app="$upper_name" 'toupper($1) == app { print tolower($2); exit }' APP_LINKING.LST)
    fi
    local mode="${CYKOS_LINK_MODE:-${manifest_mode:-$fallback}}"
    if [ "$mode" != "dynamic" ] && [ "$mode" != "static" ]; then
        echo "Error: invalid link mode '$mode' for $upper_name (use dynamic or static)." >&2
        return 1
    fi
    printf '%s' "$mode"
}

build_rust_app() {
    local src="$1"
    local name="$2"
    local upper_name=$(echo "$name" | tr '[:lower:]' '[:upper:]')
    local out_bin="apps/bin/${upper_name}.BIN"
    local mode
    mode=$(app_link_mode "$upper_name")

    echo "Compiling Rust App: $src -> $out_bin"

    rustc --target i686-unknown-linux-gnu -O -C debuginfo=0 \
        -C overflow-checks=no \
        -C relocation-model=pic \
        -C panic=abort \
        -C target-feature=-sse,-sse2 \
        --emit=obj "$src" -o "apps/bin/${name}.o"

    if [ "$mode" = "static" ]; then
        $LD -m elf_i386 -T src/user/static_elf.ld --gc-sections --no-dynamic-linker -s \
            -u main "apps/bin/${name}.o" -o "$out_bin"
    else
        $LD -m elf_i386 -shared -Bsymbolic --hash-style=sysv --gc-sections -e main -s \
            "apps/bin/${name}.o" -o "$out_bin"
    fi

    rm -f "apps/bin/${name}.o"
    echo "  [OK] Built $out_bin ($mode)"
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
if [ -f bin/images/os_image.bin ]; then
    echo "Updating FAT16 filesystem on bin/images/os_image.bin..."
    if command -v python3 &> /dev/null; then
        python3 makeext2.py bin/images/os_image.bin
        python3 install_bios.py bin/images/os_image.bin
    elif command -v python &> /dev/null; then
        python makeext2.py bin/images/os_image.bin
        python install_bios.py bin/images/os_image.bin
    fi
fi

echo "Done! Run QEMU to execute your userland apps."
