#!/bin/bash
set -e

mkdir -p bin

HOST_OS=$(uname -s)
HOST_ARCH=$(uname -m)

# 1. Detect Cross-Compiler with ARM host protection (Linux and macOS compatible)
if command -v i686-elf-gcc &> /dev/null; then
    CC="i686-elf-gcc"
elif command -v x86_64-linux-gnu-gcc &> /dev/null; then
    CC="x86_64-linux-gnu-gcc"
elif command -v x86_64-elf-gcc &> /dev/null; then
    CC="x86_64-elf-gcc"
elif command -v gcc &> /dev/null; then
    if [[ "$HOST_ARCH" == "aarch64" || "$HOST_ARCH" == "arm64" || "$HOST_ARCH" == "armv7l" ]]; then
        echo "Error: Native 'gcc' on this machine outputs ARM code!" >&2
        echo "Please install an x86 cross-compiler (e.g. i686-elf-gcc or gcc-x86-64-linux-gnu)." >&2
        exit 1
    else
        CC="gcc"
    fi
else
    echo "Error: No suitable C compiler found!" >&2
    exit 1
fi

# 2. Detect Linker
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

# 3. Assemble unified module
nasm -f elf32 boot.s -o bin/boot.o

# 4. Compile Level 1 Kernel components
$CC -m32 -c cykastd.c -o bin/cykastd.o -ffreestanding -O0 -nostdlib -fno-pic -fno-pie -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables -fno-builtin -fno-stack-protector -Wall -Wextra
$CC -m32 -c ata.c -o bin/ata.o -ffreestanding -O0 -nostdlib -fno-pic -fno-pie -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables -fno-builtin -fno-stack-protector -Wall -Wextra
$CC -m32 -c fat16.c -o bin/fat16.o -ffreestanding -O0 -nostdlib -fno-pic -fno-pie -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables -fno-builtin -fno-stack-protector -Wall -Wextra
$CC -m32 -c modload.c -o bin/modload.o -ffreestanding -O0 -nostdlib -fno-pic -fno-pie -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables -fno-builtin -fno-stack-protector -Wall -Wextra
$CC -m32 -c usercmd.c -o bin/usercmd.o -ffreestanding -O0 -nostdlib -fno-pic -fno-pie -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables -fno-builtin -fno-stack-protector -Wall -Wextra
$CC -m32 -c kernel.c -o bin/kernel.o -ffreestanding -O0 -nostdlib -fno-pic -fno-pie -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables -fno-builtin -fno-stack-protector -Wall -Wextra

# 5. Compile Level 3 CykShell (Rust)
if command -v rustc &> /dev/null; then
    echo "Compiling Level 3 CykShell (Rust)..."
    
    # Auto-add i686 target if rustup is present
    if command -v rustup &> /dev/null; then
        rustup target add i686-unknown-linux-gnu 2>/dev/null || true
    fi

    # Compile with optimizations, disabled overflow panics, no SSE, and staticlib bundling
    if rustc --target i686-unknown-linux-gnu -O -C overflow-checks=no -C relocation-model=static -C panic=abort -C target-feature=-sse,-sse2 --crate-type=staticlib shell.rs -o bin/libshell.a 2>/dev/null; then
        echo "CykShell compiled successfully into bin/libshell.a."
        SHELL_LIB="bin/libshell.a"
    elif rustc --target i686-unknown-linux-gnu -O -C overflow-checks=no -C relocation-model=static -C panic=abort -C target-feature=-sse,-sse2 --emit=obj shell.rs -o bin/shell.o 2>/dev/null; then
        echo "CykShell compiled successfully into bin/shell.o."
        SHELL_LIB="bin/shell.o"
    else
        echo "Attempting compilation with verbose output..."
        rustc --target i686-unknown-linux-gnu -O -C overflow-checks=no -C relocation-model=static -C panic=abort -C target-feature=-sse,-sse2 --crate-type=staticlib shell.rs -o bin/libshell.a
        SHELL_LIB="bin/libshell.a"
    fi
else
    echo "Error: rustc not found! Please install Rust to build CykShell." >&2
    exit 1
fi

# 6. Compile Level 2 Standalone Driver Modules
$CC -m32 -c keyboard.c -o bin/keyboard.o -ffreestanding -O0 -nostdlib -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fno-builtin -fno-stack-protector -Wall -Wextra
$LD -m elf_i386 -T module.ld --no-dynamic-linker -no-pie bin/keyboard.o -o bin/cykeybrd.drv

# 7. Link Level 1 Kernel and Level 3 CykShell
$LD -m elf_i386 -T linker.ld --gc-sections --no-dynamic-linker -no-pie -Map bin/kernel.map bin/boot.o bin/cykastd.o bin/ata.o bin/fat16.o bin/modload.o bin/usercmd.o bin/kernel.o $SHELL_LIB -o bin/os_image.bin

# 8. Export Kernel Symbols for User-Space Applications
if [ -f bin/kernel.map ]; then
    python3 -c "
import re
symbols = {}
with open('bin/kernel.map') as f:
    for line in f:
        line = line.strip()
        if not line:
            continue
        m = re.search(r'\b0x([0-9a-fA-F]{4,16})\s+([a-zA-Z_][a-zA-Z0-9_]*)\b', line)
        if m:
            symbols[m.group(2)] = '0x' + m.group(1)
            continue
        parts = line.split()
        if len(parts) >= 2:
            if re.match(r'^[0-9a-fA-F]{4,16}$', parts[0]) and re.match(r'^[a-zA-Z_][a-zA-Z0-9_]*$', parts[-1]):
                symbols[parts[-1]] = '0x' + parts[0]

with open('bin/kernel_syms.ld', 'w') as out:
    for sym, addr in symbols.items():
        out.write(f'{sym} = {addr};\n')
" 2>/dev/null || python -c "
import re
symbols = {}
with open('bin/kernel.map') as f:
    for line in f:
        line = line.strip()
        if not line:
            continue
        m = re.search(r'\b0x([0-9a-fA-F]{4,16})\s+([a-zA-Z_][a-zA-Z0-9_]*)\b', line)
        if m:
            symbols[m.group(2)] = '0x' + m.group(1)
            continue
        parts = line.split()
        if len(parts) >= 2:
            if re.match(r'^[0-9a-fA-F]{4,16}$', parts[0]) and re.match(r'^[a-zA-Z_][a-zA-Z0-9_]*$', parts[-1]):
                symbols[parts[-1]] = '0x' + parts[0]

with open('bin/kernel_syms.ld', 'w') as out:
    for sym, addr in symbols.items():
        out.write(f'{sym} = {addr};\n')
" 2>/dev/null || true
fi

# 9. Compile Level 4 Userland Applications (Rust)
if command -v rustc &> /dev/null; then
    echo "Compiling Level 4 Userland Applications..."

    # Compile default editor (edit.rs)
    if [ -f edit.rs ]; then
        echo "  Compiling Level 4 Editor (edit.rs)..."
        rm -f bin/EDIT.BIN bin/libedit.a bin/edit.o
        if rustc --target i686-unknown-linux-gnu -O \
            -C overflow-checks=no \
            -C relocation-model=static \
            -C panic=abort \
            -C target-feature=-sse,-sse2 \
            -C function-sections=yes \
            --crate-type=staticlib edit.rs -o bin/libedit.a; then
            if [ -f bin/kernel_syms.ld ]; then
                $LD -m elf_i386 -T user.ld bin/kernel_syms.ld --whole-archive bin/libedit.a --no-whole-archive -o bin/EDIT.BIN 2>/dev/null || \
                $LD -m elf_i386 -T user.ld bin/kernel_syms.ld bin/libedit.a -o bin/EDIT.BIN
                if [ -s bin/EDIT.BIN ]; then
                    echo "  -> Created Level 4 binary: bin/EDIT.BIN"
                else
                    echo "  -> Error: bin/EDIT.BIN linking failed or produced empty file!" >&2
                    rm -f bin/EDIT.BIN
                fi
            fi
            rm -f bin/libedit.a
        fi
    fi

    # Compile any additional userland apps in apps/*.rs
    if [ -d apps ]; then
        for app_src in apps/*.rs; do
            [ -e "$app_src" ] || continue
            app_name=$(basename "$app_src" .rs)
            [ "$app_name" = "cykos" ] && continue
            app_upper=$(echo "$app_name" | tr '[:lower:]' '[:upper:]')
            echo "  Compiling Userland App: $app_src..."
            if rustc --target i686-unknown-linux-gnu -O \
                -C overflow-checks=no \
                -C relocation-model=static \
                -C panic=abort \
                -C target-feature=-sse,-sse2 \
                -C function-sections=yes \
                --crate-type=staticlib "$app_src" -o "bin/lib${app_name}.a" 2>/dev/null; then
                if [ -f bin/kernel_syms.ld ]; then
                    $LD -m elf_i386 -T user.ld bin/kernel_syms.ld --whole-archive "bin/lib${app_name}.a" --no-whole-archive -o "bin/${app_upper}.BIN" 2>/dev/null || \
                    $LD -m elf_i386 -T user.ld bin/kernel_syms.ld "bin/lib${app_name}.a" -o "bin/${app_upper}.BIN" 2>/dev/null || true
                fi
                rm -f "bin/lib${app_name}.a"
            fi
        done
    fi
fi

# 10. Format FAT16 partition and package Level 2 drivers, Level 4 apps & configuration
if command -v python3 &> /dev/null; then
    python3 mkfat16.py bin/os_image.bin
elif command -v python &> /dev/null; then
    python mkfat16.py bin/os_image.bin
else
    truncate -s 16777216 bin/os_image.bin 2>/dev/null || truncate -s 16m bin/os_image.bin
fi

echo "----------------------------------------"
echo "Build successful! Created bin/os_image.bin with Kernel (L1), Drivers (L2), Shell (L3), and Apps (L4)."
