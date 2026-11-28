#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

CC="${1:-${CC:-gcc}}"
LD="${2:-${LD:-ld}}"
MANIFEST="${DRIVERS_MANIFEST:-DRIVERS.LST}"

if [[ ! -f "$MANIFEST" ]]; then
    echo "Error: driver manifest not found: $MANIFEST" >&2
    exit 1
fi

mkdir -p bin
while IFS= read -r line || [[ -n "$line" ]]; do
    read -r source output extra <<< "$line"
    [[ -z "${source:-}" || "$source" == \#* ]] && continue
    if [[ -z "${output:-}" || -n "${extra:-}" ]]; then
        echo "Error: expected 'source output' in $MANIFEST: $source ${output:-} ${extra:-}" >&2
        exit 1
    fi
    if [[ ! -f "$source" ]]; then
        echo "Error: driver source not found: $source" >&2
        exit 1
    fi

    module_name="$(basename "$source" .c)"
    object="bin/${module_name}.driver.o"
    mkdir -p "$(dirname "$output")"

    echo "Building relocatable driver: $source -> $output"
    "$CC" -m32 -DMODULE_LOAD_ADDRESS=0 -c "$source" -o "$object" \
        -ffreestanding -O2 -nostdlib -fPIC -fno-asynchronous-unwind-tables \
        -fno-builtin -fno-stack-protector -Wall -Wextra -Isrc/kernel -Isrc/storage -Isrc/drivers
    "$LD" -m elf_i386 -shared -Bsymbolic --hash-style=sysv --gc-sections -e 0 \
        "$object" -o "$output"
    rm -f "$object"
done < "$MANIFEST"
