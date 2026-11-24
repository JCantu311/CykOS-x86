#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
SYSROOT="$ROOT/build/cykos-sysroot"

if command -v i686-elf-gcc >/dev/null 2>&1; then
    CC=$(command -v i686-elf-gcc)
    AR=$(command -v i686-elf-ar || command -v ar)
    TARGET_FLAGS=()
elif command -v i686-linux-gnu-gcc >/dev/null 2>&1; then
    CC=$(command -v i686-linux-gnu-gcc)
    AR=$(command -v i686-linux-gnu-ar || command -v ar)
    TARGET_FLAGS=()
elif command -v gcc >/dev/null 2>&1; then
    CC=$(command -v gcc)
    AR=$(command -v ar)
    TARGET_FLAGS=(-m32)
else
    echo "No GCC compiler found (expected i686-elf-gcc, i686-linux-gnu-gcc, or gcc)." >&2
    exit 1
fi

mkdir -p "$SYSROOT/include" "$SYSROOT/lib" "$ROOT/build/cykos-libc-obj"
cp -R "$ROOT/libc/include/." "$SYSROOT/include/"
cp "$ROOT/src/user/static_elf.ld" "$SYSROOT/cykos-static-elf.ld"

CFLAGS=("${TARGET_FLAGS[@]}" -ffreestanding -O2 -fPIC -ffunction-sections
    -fdata-sections -fno-asynchronous-unwind-tables -fno-builtin
    -fno-stack-protector -Wall -Wextra -nostdinc
    -isystem "$SYSROOT/include" -isystem "$($CC -print-file-name=include)")

for source in "$ROOT"/libc/src/*.c; do
    name=$(basename "$source" .c)
    "$CC" "${CFLAGS[@]}" -c "$source" -o "$ROOT/build/cykos-libc-obj/$name.o"
done
"$CC" "${TARGET_FLAGS[@]}" -ffreestanding -fPIC -c "$ROOT/libc/src/setjmp.S" \
    -o "$ROOT/build/cykos-libc-obj/setjmp.o"
"$AR" rcs "$SYSROOT/lib/libc.a" "$ROOT"/build/cykos-libc-obj/*.o
"$AR" rcs "$SYSROOT/lib/libm.a" "$ROOT/build/cykos-libc-obj/math.o"
"$CC" "${CFLAGS[@]}" -c "$ROOT/libc/src/crt0.c" -o "$SYSROOT/lib/crt0.o"

echo "CykOS GCC sysroot ready: $SYSROOT"
echo "Add $ROOT/toolchain/bin to PATH to use i686-cykos-gcc."
