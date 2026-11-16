#!/bin/bash
set -euo pipefail

CC="${1:-${CC:-gcc}}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DOOM_DIR="$ROOT/third_party/doomgeneric/doomgeneric"
DOOM_OUTPUT="../../../bin/DOOM.BIN"

if [ ! -f "$DOOM_DIR/Makefile" ] || [ ! -f "$DOOM_DIR/doomgeneric.c" ]; then
    echo "DoomGeneric source is missing; clone ozkl/doomgeneric into third_party/doomgeneric." >&2
    exit 1
fi

mkdir -p "$ROOT/bin"
rm -f "$ROOT/bin/DOOM.BIN"

# Follow the upstream porting guide: provide a platform implementation of the
# DG_* functions, and let doomgeneric_Create/Tick drive the game. Override only
# the platform object in the stock Makefile's source list.
DOOM_OBJECTS="$(sed -n 's/^SRC_DOOM = //p' "$DOOM_DIR/Makefile" | head -n 1 | tr -d '\r')"
if [ -z "$DOOM_OBJECTS" ] || [[ "$DOOM_OBJECTS" != *doomgeneric_xlib.o* ]]; then
    echo "Could not find the upstream platform object in the DoomGeneric Makefile." >&2
    exit 1
fi
DOOM_OBJECTS="${DOOM_OBJECTS/doomgeneric_xlib.o/doomgeneric_cykos.o}"
make -C "$DOOM_DIR" -f Makefile clean OBJDIR=build OUTPUT="$DOOM_OUTPUT" \
    SRC_DOOM="$DOOM_OBJECTS"

DOOM_CFLAGS=(
    -m32 -ffreestanding -Os -fno-builtin -fno-stack-protector -fno-pic -fno-pie
    -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables
    -DNORMALUNIX -D_DEFAULT_SOURCE -DCMAP256 -DCYKOS
    -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200
    -I../../../libc/include -I.
)

# The compiler driver selects its i386 libgcc for -m32. -nostdlib disables
# host startup objects and host libc; only the explicit CykOS archives and the
# target's static compiler-runtime archive are linked.
LIBS="../../../bin/kernel_syms.ld ../../../bin/libc.a -lgcc"

echo "  -> Building and linking with DoomGeneric's stock Makefile"
make -C "$DOOM_DIR" -f Makefile \
    CC="$CC" OBJDIR=build OUTPUT="$DOOM_OUTPUT" \
    SRC_DOOM="$DOOM_OBJECTS" VPATH=../../../apps/doom \
    CFLAGS="${DOOM_CFLAGS[*]}" \
    LDFLAGS="-nostdlib -static -static-libgcc -no-pie -Wl,-m,elf_i386 -Wl,-T,../../../src/user/static_elf.ld -Wl,--gc-sections -Wl,--no-dynamic-linker -Wl,-u,main -Wl,-s" \
    LIBS="$LIBS"

APP_BYTES="$(wc -c < "$ROOT/bin/DOOM.BIN")"
if [ "$APP_BYTES" -gt 524288 ]; then
    echo "Doom binary is ${APP_BYTES} bytes; CykOS's packaged app file limit is 524288 bytes." >&2
    exit 1
fi

mkdir -p "$ROOT/bin"
cp "$ROOT/third_party/universal-doom/DOOM1.WAD" "$ROOT/bin/DOOM1.WAD"
echo "  -> Created bin/DOOM.BIN (${APP_BYTES} bytes) and bundled shareware DOOM1.WAD"
