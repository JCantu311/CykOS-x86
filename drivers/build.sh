#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

CC="${1:-${CC:-gcc}}"
LD="${2:-${LD:-ld}}"
MANIFEST="${DRIVERS_MANIFEST:-DRIVERS.LST}"

if [[ ! -f bin/kernel_syms.ld ]]; then
    echo "Error: bin/kernel_syms.ld missing; build the kernel first with ./make.sh." >&2
    exit 1
fi
if [[ ! -f "$MANIFEST" ]]; then
    echo "Error: driver manifest not found: $MANIFEST" >&2
    exit 1
fi

mkdir -p bin
while IFS= read -r line || [[ -n "$line" ]]; do
    read -r source output load_address extra <<< "$line"
    [[ -z "${source:-}" || "$source" == \#* ]] && continue
    if [[ -z "${output:-}" || -z "${load_address:-}" || -n "${extra:-}" ]]; then
        echo "Error: expected 'source output load_address' in $MANIFEST: $source ${output:-} ${load_address:-} ${extra:-}" >&2
        exit 1
    fi
    if [[ ! -f "$source" ]]; then
        echo "Error: driver source not found: $source" >&2
        exit 1
    fi

    module_name="$(basename "$source" .c)"
    object="bin/${module_name}.driver.o"
    linker_script="bin/${module_name}.driver.ld"
    mkdir -p "$(dirname "$output")"

    echo "Building driver: $source -> $output at $load_address"
    "$CC" -m32 -DMODULE_LOAD_ADDRESS="$load_address" -c "$source" -o "$object" \
        -ffreestanding -O0 -nostdlib -fno-pic -fno-pie -fno-asynchronous-unwind-tables \
        -fno-builtin -fno-stack-protector -Wall -Wextra -Isrc/kernel -Isrc/storage -Isrc/drivers
    python3 -c 'import pathlib,sys; source=pathlib.Path(sys.argv[1]); target=pathlib.Path(sys.argv[2]); target.write_text(source.read_text().replace("__MODULE_LINK_BASE__", sys.argv[3]))' \
        src/drivers/module.ld "$linker_script" "$load_address"
    "$LD" -m elf_i386 -T "$linker_script" --no-dynamic-linker -no-pie \
        bin/kernel_syms.ld "$object" -o "$output"
    rm -f "$object" "$linker_script"
done < "$MANIFEST"