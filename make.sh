#!/bin/bash
set -e

# Clean stale build artifacts to prevent build inconsistencies.
# Clean build is the default: removes all object files, drivers, disk images,
# and symbol exports so every build starts from a known-clean state.
echo "Cleaning stale build artifacts..."
rm -rf bin

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
elif command -v rustc &> /dev/null && [[ -x "$(rustc --print sysroot)/lib/rustlib/$(rustc -vV | sed -n 's/^host: //p')/bin/gcc-ld/ld.lld" ]]; then
    LD="$(rustc --print sysroot)/lib/rustlib/$(rustc -vV | sed -n 's/^host: //p')/bin/gcc-ld/ld.lld"
elif command -v x86_64-elf-ld &> /dev/null; then
    LD="x86_64-elf-ld"
elif command -v ld &> /dev/null; then
    LD="ld"
else
    echo "Error: No linker found!" >&2
    exit 1
fi

# 3. Assemble unified module
nasm -f elf32 src/boot/boot.s -o bin/boot.o
nasm -f elf32 src/boot/uefi_entry.asm -o bin/uefi_entry.o
nasm -f bin src/boot/mbr.asm -o bin/mbr.bin
nasm -f bin src/boot/stage2.asm -o bin/stage2.bin

COMMON_CFLAGS="-m32 -ffreestanding -O0 -nostdlib -fno-pic -fno-pie -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables -fno-builtin -fno-stack-protector -Wall -Wextra -Isrc/kernel -Isrc/storage -Isrc/drivers"

# 4. Compile Level 1 Kernel components
echo "Compiling Level 1 Kernel components..."
for source in src/kernel/*.c src/storage/*.c; do
    name=$(basename "$source" .c)
    "$CC" $COMMON_CFLAGS -c "$source" -o "bin/${name}.o"
done

# 5. Compile Level 3 CykShell (Rust)
if command -v rustc &> /dev/null; then
    echo "Compiling Level 3 CykShell (Rust)..."
    
    # Auto-add i686 target if rustup is present
    if command -v rustup &> /dev/null; then
        rustup target add i686-unknown-linux-gnu 2>/dev/null || true
    fi

    # Compile with optimizations, disabled overflow panics, and staticlib bundling
    if rustc --target i686-unknown-linux-gnu -O -C overflow-checks=no -C relocation-model=static -C panic=abort --crate-type=staticlib src/user/shell.rs -o bin/libshell.a 2>/dev/null; then
        echo "CykShell compiled successfully into bin/libshell.a."
        SHELL_LIB="bin/libshell.a"
    elif rustc --target i686-unknown-linux-gnu -O -C overflow-checks=no -C relocation-model=static -C panic=abort --emit=obj src/user/shell.rs -o bin/shell.o 2>/dev/null; then
        echo "CykShell compiled successfully into bin/shell.o."
        SHELL_LIB="bin/shell.o"
    else
        echo "Attempting compilation with verbose output..."
        rustc --target i686-unknown-linux-gnu -O -C overflow-checks=no -C relocation-model=static -C panic=abort --crate-type=staticlib src/user/shell.rs -o bin/libshell.a
        SHELL_LIB="bin/libshell.a"
    fi
else
    echo "Error: rustc not found! Please install Rust to build CykShell." >&2
    exit 1
fi

# 6. Compile Level 2 Standalone Driver Modules
# Drivers are compiled and linked after kernel_syms.ld is generated below.

# 7. Link Level 1 Kernel and Level 3 CykShell
$LD -m elf_i386 -T config/linker.ld --gc-sections --no-dynamic-linker -no-pie -Map bin/kernel.map bin/boot.o bin/cykastd.o bin/ata.o bin/ahci.o bin/nvme.o bin/fat16.o bin/fat32.o bin/ext2.o bin/vfs.o bin/modload.o bin/usercmd.o bin/idt.o bin/syscall.o bin/permissions.o bin/heap.o bin/paging.o bin/gdt.o bin/exports.o bin/pci.o bin/kernel.o $SHELL_LIB -o bin/KERNEL.BIN

# Build an ELF32 Multiboot2 entry for GRUB's x86_64 UEFI loader. The BIOS
# image remains the flat binary above; both entry paths share the kernel C code.
$LD -m elf_i386 -T config/uefi-linker.ld --gc-sections --no-dynamic-linker -no-pie \
    -Map bin/kernel-uefi.map bin/uefi_entry.o bin/boot.o bin/cykastd.o bin/ata.o \
    bin/ahci.o bin/nvme.o bin/fat16.o bin/fat32.o bin/ext2.o bin/vfs.o bin/modload.o \
    bin/usercmd.o bin/idt.o bin/syscall.o bin/permissions.o bin/heap.o \
    bin/paging.o bin/gdt.o bin/exports.o bin/pci.o bin/kernel.o $SHELL_LIB \
    -o bin/KERNEL.ELF
if command -v grub-file &> /dev/null; then
    grub-file --is-x86-multiboot2 bin/KERNEL.ELF
fi
if command -v grub-mkstandalone &> /dev/null; then
    grub-mkstandalone --format=x86_64-efi --output=bin/BOOTX64.EFI \
        --compress=xz --locales='' --fonts='' --themes='' \
        --install-modules="part_msdos fat multiboot2 normal terminal efi_gop video video_fb gfxterm font" \
        --modules="part_msdos fat multiboot2 normal terminal efi_gop video video_fb gfxterm font" \
        "boot/grub/grub.cfg=tools/grub-uefi.cfg"
else
    echo "WARNING: grub-mkstandalone missing; the disk will not be UEFI-bootable."
fi

# 8. Export Kernel Symbols for User-Space Applications
if [ -f bin/kernel.map ]; then
    python3 -c "
import re, sys
symbols = {}
with open('bin/kernel.map') as f:
    for line in f:
        line = line.strip()
        if not line:
            continue
        # Format 1: GNU ld (0x00007e00 symbol_name)
        m = re.search(r'\b0x([0-9a-fA-F]{4,16})\s+([a-zA-Z_][a-zA-Z0-9_]*)\b', line)
        if m:
            val = int(m.group(1), 16)
            name = m.group(2)
            if 0x10000 <= val < 0x90000 and name not in symbols:
                symbols[name] = f'0x{val:08x}'
            continue
        # Format 2: ld.lld (00007e00 ... symbol_name)
        parts = line.split()
        if len(parts) >= 2:
            if re.match(r'^[0-9a-fA-F]{4,16}$', parts[0]) and re.match(r'^[a-zA-Z_][a-zA-Z0-9_]*$', parts[-1]):
                val = int(parts[0], 16)
                name = parts[-1]
                if 0x10000 <= val < 0x90000 and name not in symbols:
                    symbols[name] = f'0x{val:08x}'

with open('bin/kernel_syms.ld', 'w') as out:
    for sym, addr in sorted(symbols.items()):
        out.write(f'{sym} = {addr};\n')

key_syms = [s for s in ['print_string', 'print_char', 'fat16_read_file_by_name', 'fat16_write_file'] if s in symbols]
print(f'  -> Exported {len(symbols)} kernel symbols. Key symbols found: {\", \".join(key_syms)}')
" 2>/dev/null || true
fi

./drivers/build.sh "$CC" "$LD"

# Link the standalone Level 3 shell against kernel symbols.
python3 -c "
with open('bin/kernel_syms.ld') as source, open('bin/kernel_syms_shell.ld', 'w') as target:
    for line in source:
        if not line.startswith('shell_main = '):
            target.write(line)
"
$LD -m elf_i386 -T src/user/shell.ld --gc-sections -u main bin/kernel_syms_shell.ld bin/libshell.a -o bin/SHELL.BIN
rm -f bin/kernel_syms_shell.ld

# 9. Compile Level 4 Userland Applications (Rust)
if command -v rustc &> /dev/null; then
    echo "Compiling Level 4 Userland Applications..."

    RUST_FLAGS="--target i686-unknown-linux-gnu -O -C debuginfo=0 -C overflow-checks=no -C relocation-model=pic -C panic=abort"

    app_link_mode() {
        local app_upper="$1"
        local fallback="${2:-static}"
        local manifest_mode=""
        if [ -f APP_LINKING.LST ]; then
            manifest_mode=$(awk -v app="$app_upper" 'toupper($1) == app { print tolower($2); exit }' APP_LINKING.LST)
        fi
        local mode="${CYKOS_LINK_MODE:-${manifest_mode:-$fallback}}"
        if [ "$mode" != "dynamic" ] && [ "$mode" != "static" ]; then
            echo "Error: invalid link mode '$mode' for $app_upper (use dynamic or static)." >&2
            return 1
        fi
        printf '%s' "$mode"
    }

    # Link as ET_DYN for runtime imports or ET_EXEC for a fully linked image.
    link_user_app() {
        local input="$1"
        local output="$2"
        local app_upper="$3"
        local mode
        mode=$(app_link_mode "$app_upper" static) || return 1
        if [ "$mode" = "static" ]; then
            $LD -m elf_i386 -T src/user/static_elf.ld --gc-sections --no-dynamic-linker -s \
                -u main "$input" -o "$output"
        else
            $LD -m elf_i386 -shared -Bsymbolic --hash-style=sysv --gc-sections \
                -e main -s "$input" -o "$output"
        fi
        echo "  -> $app_upper link mode: $mode"
    }

    # Compile default editor (edit.rs)
    if [ -f src/user/edit.rs ]; then
        echo "  Compiling Level 4 Editor (edit.rs)..."
        rm -f bin/EDIT.BIN bin/libedit.a bin/edit.o

        # Strategy 1: Try --emit=obj (produces small binary, no libcore bloat)
        if rustc $RUST_FLAGS --emit=obj src/user/edit.rs -o bin/edit.o; then
            if link_user_app bin/edit.o bin/EDIT.BIN EDIT; then
                echo "  -> Created Level 4 binary: bin/EDIT.BIN (from .o)"
                rm -f bin/edit.o
            else
                rm -f bin/edit.o bin/EDIT.BIN
                echo "  -> obj linking had unresolved symbols, trying staticlib..."
            fi
        fi

        # Strategy 2: Fall back to --crate-type=staticlib if .o approach failed
        if [ ! -f bin/EDIT.BIN ]; then
            if rustc $RUST_FLAGS --crate-type=staticlib src/user/edit.rs -o bin/libedit.a; then
                if link_user_app bin/libedit.a bin/EDIT.BIN EDIT; then
                    echo "  -> Created Level 4 binary: bin/EDIT.BIN (from staticlib)"
                else
                    echo "  -> ERROR: Failed to link EDIT.BIN from staticlib!" >&2
                    rm -f bin/EDIT.BIN
                fi
                rm -f bin/libedit.a
            else
                echo "  -> ERROR: Failed to compile edit.rs!" >&2
            fi
        fi

        # Report binary size
        if [ -f bin/EDIT.BIN ]; then
            sz=$(wc -c < bin/EDIT.BIN)
            echo "  -> EDIT.BIN size: $sz bytes"
            if [ "$sz" -gt 524288 ]; then
                echo "  -> WARNING: EDIT.BIN ($sz bytes) exceeds USER_APP_FILE_MAX (524288)!" >&2
            fi
        fi
    fi

    # Compile any additional userland apps in apps/*.rs
    if [ -d apps ]; then
        while IFS= read -r app_src; do
            app_name=$(basename "$app_src" .rs)
            [ "$app_name" = "cykos" ] && continue
            app_upper=$(echo "$app_name" | tr '[:lower:]' '[:upper:]')
            echo "  Compiling Userland App: $app_src..."

            # CRITICAL: Clean old binary before rebuilding
            rm -f "bin/${app_upper}.BIN" "bin/lib${app_name}.a" "bin/${app_name}.o"

            # Strategy 1: Try --emit=obj first
            if rustc $RUST_FLAGS --emit=obj "$app_src" -o "bin/${app_name}.o" 2>/dev/null; then
                if link_user_app "bin/${app_name}.o" "bin/${app_upper}.BIN" "$app_upper" 2>/dev/null; then
                    echo "  -> Created: bin/${app_upper}.BIN (from .o)"
                    rm -f "bin/${app_name}.o"
                else
                    rm -f "bin/${app_name}.o" "bin/${app_upper}.BIN"
                fi
            fi

            # Strategy 2: Fall back to staticlib
            if [ ! -f "bin/${app_upper}.BIN" ]; then
                if rustc $RUST_FLAGS --crate-type=staticlib "$app_src" -o "bin/lib${app_name}.a" 2>/dev/null; then
                    if link_user_app "bin/lib${app_name}.a" "bin/${app_upper}.BIN" "$app_upper" 2>/dev/null; then
                        echo "  -> Created: bin/${app_upper}.BIN (from staticlib)"
                    else
                        echo "  -> ERROR: Failed to link ${app_upper}.BIN!" >&2
                        rm -f "bin/${app_upper}.BIN"
                    fi
                    rm -f "bin/lib${app_name}.a"
                else
                    echo "  -> ERROR: Failed to compile $app_src!" >&2
                fi
            fi

            # Report binary size
            if [ -f "bin/${app_upper}.BIN" ]; then
                sz=$(wc -c < "bin/${app_upper}.BIN")
                echo "  -> ${app_upper}.BIN size: $sz bytes"
                if [ "$sz" -gt 196608 ]; then
                    echo "  -> WARNING: ${app_upper}.BIN ($sz bytes) exceeds USER_APP_MAX_SIZE (196608)!" >&2
                fi
            fi
        done < <(find apps -type f -name '*.rs' | sort)
    fi
fi

# 9b. Compile POSIX C Standard Library (libc.a) & C Userland Applications
echo "Compiling CykOS POSIX C Standard Library (libc)..."
LIBC_CFLAGS="-m32 -ffreestanding -O2 -nostdlib -fPIC -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables -fno-builtin -fno-stack-protector -Wall -Wextra -Ilibc/include"
mkdir -p bin/libc_objs
for libc_src in libc/src/*.c; do
    [ -e "$libc_src" ] || continue
    obj_name=$(basename "$libc_src" .c)
    "$CC" $LIBC_CFLAGS -c "$libc_src" -o "bin/libc_objs/${obj_name}.o"
done
if [ -f libc/src/setjmp.S ]; then
    "$CC" -m32 -ffreestanding -fPIC -c libc/src/setjmp.S -o bin/libc_objs/setjmp.o
fi

# Pack libc.a archive using ar (or llvm-ar)
if command -v i686-elf-ar &> /dev/null; then
    AR="i686-elf-ar"
elif command -v x86_64-linux-gnu-ar &> /dev/null; then
    AR="x86_64-linux-gnu-ar"
elif command -v llvm-ar &> /dev/null; then
    AR="llvm-ar"
elif command -v ar &> /dev/null; then
    AR="ar"
else
    AR="ar"
fi

$AR rcs bin/libc.a bin/libc_objs/*.o
echo "  -> Created: bin/libc.a"

# Compile any C userland applications in apps/*.c
if [ -d apps ]; then
    for c_app in apps/*.c; do
        [ -e "$c_app" ] || continue
        app_name=$(basename "$c_app" .c)
        app_upper=$(echo "$app_name" | tr '[:lower:]' '[:upper:]')
        app_mode=$(app_link_mode "$app_upper" static)
        echo "  Compiling C Userland App: $c_app..."
        rm -f "bin/${app_upper}.BIN" "bin/${app_name}.o"

        "$CC" $LIBC_CFLAGS -c "$c_app" -o "bin/${app_name}.o"
        if [ "$app_mode" = "dynamic" ]; then
            $LD -m elf_i386 -shared -Bsymbolic --hash-style=sysv --gc-sections \
                -e _start -u _start "bin/${app_name}.o" bin/libc.a -o "bin/${app_upper}.BIN"
        else
            $LD -m elf_i386 -T src/user/static_elf.ld -e _start --gc-sections -u _start \
                "bin/${app_name}.o" bin/libc.a -o "bin/${app_upper}.BIN"
        fi
        echo "  -> $app_upper link mode: $app_mode"
        rm -f "bin/${app_name}.o"

        if [ -f "bin/${app_upper}.BIN" ]; then
            sz=$(wc -c < "bin/${app_upper}.BIN")
            echo "  -> Created C binary: bin/${app_upper}.BIN ($sz bytes)"
        fi
    done
fi

# Build the Lua interpreter from the upstream Lua core and the CykOS host.
if [ -d third_party/lua ] && [ -f ports/lua/main.c ]; then
    echo "Compiling Lua for CykOS..."
    mkdir -p bin/lua_objs
    LUA_CFLAGS="-m32 -ffreestanding -O2 -nostdlib -fPIC -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables -fno-builtin -fno-stack-protector -Wall -Wextra -Iports/lua/include -Ilibc/include -Ithird_party/lua -include ports/lua/cykos_compat.h -DLUA_CYKOS -Dluai_makeseed=cykos_lua_makeseed -Dl_strcoll=strcmp"
    LUA_SOURCES="lapi lcode lctype ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes lparser lstate lstring ltable ltm lundump lvm lzio lauxlib lbaselib lcorolib lstrlib ltablib lutf8lib"
    for lua_source in $LUA_SOURCES; do
        "$CC" $LUA_CFLAGS -c "third_party/lua/${lua_source}.c" -o "bin/lua_objs/${lua_source}.o"
    done
    "$CC" $LUA_CFLAGS -c ports/lua/cykos_compat.c -o bin/lua_objs/cykos_compat.o
    "$CC" $LUA_CFLAGS -c ports/lua/main.c -o bin/lua_objs/main.o
    $LD -m elf_i386 -T src/user/static_elf.ld -e _start --gc-sections -u _start \
        bin/lua_objs/*.o bin/libc.a -o bin/LUA.BIN
    lua_size=$(wc -c < bin/LUA.BIN)
    echo "  -> Created: bin/LUA.BIN ($lua_size bytes)"
    if [ "$lua_size" -gt 524288 ]; then
        echo "  -> WARNING: LUA.BIN exceeds USER_APP_FILE_MAX (524288)!" >&2
    fi
fi

# Build DoomGeneric against the CykOS VGA 320x200 indexed-color interface.
if [ -f apps/doom/build.sh ]; then
    echo "Compiling DoomGeneric for CykOS..."
    bash apps/doom/build.sh "$CC" "$LD"
fi

# 9c. Build MODULES.CFG from DRIVERS.LST manifest
echo "Building MODULES.CFG from DRIVERS.LST manifest..."
if [ -f "DRIVERS.LST" ]; then
    echo "  -> Generating MODULES.CFG from DRIVERS.LST..."
    # Shell-based parsing of DRIVERS.LST
    # Format: each line is "source output"; modules are relocated at runtime.
    # Extract driver output filenames and generate MODULES.CFG entries
    printf "# CykOS Boot-Time Kernel Module List\n" > bin/MODULES.CFG
    while IFS= read -r line || [[ -n "$line" ]]; do
        # Skip comments and empty lines
        [ -z "$line" ] && continue
        echo "$line" | grep -q '^#' && continue
        # Extract the output filename (second field: the .drv file path)
        driver_file=$(echo "$line" | awk '{print $2}')
        [ -z "$driver_file" ] && continue
        # Extract just the filename (remove any directory path keeping only the .drv name)
        driver_filename="${driver_file##*/}"
        # FAT16 uses 8.3 names. Match the base-name truncation performed by
        # makeext2.py so module paths resolve to the directory entries on disk.
        driver_stem_upper=$(echo "${driver_filename%.*}" | tr '[:lower:]' '[:upper:]')
        driver_ext_upper=$(echo "${driver_filename##*.}" | tr '[:lower:]' '[:upper:]')
        driver_file_upper="${driver_stem_upper:0:8}.${driver_ext_upper}"
        printf "/BOOT/DRIVERS/%s\n" "$driver_file_upper" >> bin/MODULES.CFG
    done < DRIVERS.LST
else
    echo "  -> WARNING: DRIVERS.LST not found, generating default MODULES.CFG..."
    cat > bin/MODULES.CFG << 'MODULES_EOF'
# CykOS Boot-Time Kernel Module List
/BOOT/DRIVERS/CYKEYBRD.DRV
/BOOT/DRIVERS/CYKDISK.DRV
/BOOT/DRIVERS/CYSOUND.DRV
/BOOT/DRIVERS/CYHDA.DRV
/BOOT/DRIVERS/CYDISPLA.DRV
/BOOT/DRIVERS/CYKUSB.DRV
MODULES_EOF
fi

# Ensure BOOT/DRIVERS directory exists in the image
# (makeext2.py will handle injection, but we verify drivers exist)
if [ -d bin/DRIVERS ]; then
    echo "  -> Verifying driver files for MODULES.CFG..."
    while IFS= read -r driver_line; do
        # Skip comments and empty lines
        [ -z "$driver_line" ] && continue
        echo "$driver_line" | grep -q '^#' && continue
        driver_file=$(echo "$driver_line" | xargs)
        driver_filename="${driver_file##*/}"
        if [ ! -f "bin/DRIVERS/${driver_filename}" ]; then
            echo "  -> WARNING: Driver ${driver_filename} not found in bin/DRIVERS/"
        fi
    done < bin/MODULES.CFG
fi

# 10. Build FAT16 BIOS boot + ext2 root partitions and package the OS tree
mkdir -p bin/images
if command -v python3 &> /dev/null; then
    python3 makeext2.py bin/images/os_image.bin
    python3 install_bios.py bin/images/os_image.bin
elif command -v python &> /dev/null; then
    python makeext2.py bin/images/os_image.bin
    python install_bios.py bin/images/os_image.bin
else
    truncate -s 16777216 bin/images/os_image.bin 2>/dev/null || truncate -s 16m bin/images/os_image.bin
fi

# 11. Publish raw IMG and convert to desktop hypervisor formats when supported.
cp bin/images/os_image.bin bin/images/cykos.img
if command -v VBoxManage &> /dev/null; then
    VBoxManage convertfromraw bin/images/os_image.bin bin/images/cykos.vdi --format VDI
    if ! command -v qemu-img &> /dev/null; then
        VBoxManage convertfromraw bin/images/os_image.bin bin/images/cykos.vmdk --format VMDK
        VBoxManage convertfromraw bin/images/os_image.bin bin/images/cykos.vhd --format VHD
    fi
fi
if command -v qemu-img &> /dev/null; then
    qemu-img convert -f raw -O vmdk bin/images/os_image.bin bin/images/cykos.vmdk
    qemu-img convert -f raw -O vpc bin/images/os_image.bin bin/images/cykos.vhd
    if ! command -v VBoxManage &> /dev/null; then
        qemu-img convert -f raw -O vdi bin/images/os_image.bin bin/images/cykos.vdi
    fi
fi

# Build the BIOS El Torito live/installer CD. Live mode loads the kernel from
# the ISO and uses IDE ATAPI to stage the embedded disk image in RAM; install
# mode writes that image through BIOS EDD. xorriso is optional for normal builds.
if command -v xorriso &> /dev/null && command -v python3 &> /dev/null; then
    echo "Building BIOS/UEFI CD installer ISO..."
    python3 tools/build-installer-iso.py \
        --image bin/images/os_image.bin \
        --kernel-elf bin/KERNEL.ELF \
        --output bin/images/cykos-installer.iso
else
    echo "WARNING: Skipping BIOS CD installer ISO (requires xorriso and python3)."
fi

echo "----------------------------------------"
echo "Build successful! Disk images and, when available, cykos-installer.iso are in bin/images."
