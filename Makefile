# CykOS Parallel Makefile (equivalent to make.sh, safe for make -j)

SHELL := /bin/bash

HOST_ARCH := $(shell uname -m)

# 1. Detect Cross-Compiler with ARM host protection
CC := $(shell \
	if command -v i686-elf-gcc >/dev/null 2>&1; then echo i686-elf-gcc; \
	elif command -v x86_64-linux-gnu-gcc >/dev/null 2>&1; then echo x86_64-linux-gnu-gcc; \
	elif command -v x86_64-elf-gcc >/dev/null 2>&1; then echo x86_64-elf-gcc; \
	elif command -v gcc >/dev/null 2>&1; then echo gcc; \
	fi)

ifeq ($(CC),)
$(error No suitable C compiler found)
endif
ifeq ($(CC),gcc)
ifneq ($(filter aarch64 arm64 armv7l,$(HOST_ARCH)),)
$(error Native 'gcc' on this machine outputs ARM code! Please install an x86 cross-compiler)
endif
endif

# 2. Detect Linker
RUSTC_SYSROOT := $(shell rustc --print sysroot 2>/dev/null)
RUSTC_HOST := $(shell rustc -vV 2>/dev/null | sed -n 's/^host: //p')
RUSTC_LLD := $(RUSTC_SYSROOT)/lib/rustlib/$(RUSTC_HOST)/bin/gcc-ld/ld.lld

LD := $(shell \
	if command -v i686-elf-ld >/dev/null 2>&1; then echo i686-elf-ld; \
	elif command -v ld.lld >/dev/null 2>&1; then echo ld.lld; \
	elif command -v x86_64-linux-gnu-ld >/dev/null 2>&1; then echo x86_64-linux-gnu-ld; \
	elif [ -x "$(RUSTC_LLD)" ]; then echo "$(RUSTC_LLD)"; \
	elif command -v x86_64-elf-ld >/dev/null 2>&1; then echo x86_64-elf-ld; \
	elif command -v ld >/dev/null 2>&1; then echo ld; \
	fi)

ifeq ($(LD),)
$(error No linker found)
endif

# 3. Detect Archiver
AR := $(shell \
	if command -v i686-elf-ar >/dev/null 2>&1; then echo i686-elf-ar; \
	elif command -v x86_64-linux-gnu-ar >/dev/null 2>&1; then echo x86_64-linux-gnu-ar; \
	elif command -v llvm-ar >/dev/null 2>&1; then echo llvm-ar; \
	else echo ar; \
	fi)

HAS_RUSTC := $(shell command -v rustc >/dev/null 2>&1 && echo 1)
ifneq ($(HAS_RUSTC),1)
$(error rustc not found! Please install Rust to build CykShell and userland apps)
endif

HAS_GRUB_FILE := $(shell command -v grub-file >/dev/null 2>&1 && echo 1)
HAS_GRUB_MKSTANDALONE := $(shell command -v grub-mkstandalone >/dev/null 2>&1 && echo 1)
HAS_VBOXMANAGE := $(shell command -v VBoxManage >/dev/null 2>&1 && echo 1)
HAS_QEMU_IMG := $(shell command -v qemu-img >/dev/null 2>&1 && echo 1)
HAS_XORRISO := $(shell command -v xorriso >/dev/null 2>&1 && echo 1)

COMMON_CFLAGS := -m32 -ffreestanding -O2 -nostdlib -fno-pic -fno-pie \
	-fno-delete-null-pointer-checks --param=min-pagesize=0 \
	-ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables \
	-fno-builtin -fno-stack-protector -Wall -Wextra \
	-Isrc/kernel -Isrc/storage -Isrc/drivers

DRIVER_CFLAGS := -m32 -DMODULE_LOAD_ADDRESS=0 -ffreestanding -O2 -nostdlib -fPIC \
	-fno-asynchronous-unwind-tables -fno-builtin -fno-stack-protector \
	-Wall -Wextra -Isrc/kernel -Isrc/storage -Isrc/drivers

RUST_FLAGS := --target i686-unknown-linux-gnu -O -C debuginfo=0 \
	-C overflow-checks=no -C relocation-model=pic -C panic=abort

LIBC_CFLAGS := -m32 -ffreestanding -O2 -nostdlib -fPIC \
	-ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables \
	-fno-builtin -fno-stack-protector -Wall -Wextra -Ilibc/include

LUA_CFLAGS := -m32 -ffreestanding -O2 -nostdlib -fPIC \
	-ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables \
	-fno-builtin -fno-stack-protector -Wall -Wextra \
	-Iports/lua/include -Ilibc/include -Ithird_party/lua \
	-include ports/lua/cykos_compat.h -DLUA_CYKOS \
	-Dluai_makeseed=cykos_lua_makeseed -Dl_strcoll=strcmp

# Kernel & storage objects
KERNEL_SRCS := $(wildcard src/kernel/*.c src/storage/*.c)
KERNEL_OBJS := $(patsubst %.c,bin/%.o,$(notdir $(KERNEL_SRCS)))
KERNEL_LINK_OBJS := bin/boot.o bin/cykastd.o bin/ata.o bin/ahci.o bin/nvme.o \
	bin/fat16.o bin/fat32.o bin/ext2.o bin/vfs.o bin/modload.o bin/usercmd.o \
	bin/idt.o bin/syscall.o bin/permissions.o bin/accounts.o bin/heap.o \
	bin/paging.o bin/gdt.o bin/exports.o bin/pci.o bin/sched.o bin/net.o bin/kernel.o

# Drivers from DRIVERS.LST (or defaults)
ifneq ($(wildcard DRIVERS.LST),)
DRIVER_PAIRS := $(shell awk '!/^[[:space:]]*(#|$$)/ { print $$1 ":" $$2 }' DRIVERS.LST)
DRIVER_BINS := $(foreach p,$(DRIVER_PAIRS),$(word 2,$(subst :, ,$(p))))
else
DRIVER_BINS := bin/cykeybrd.drv bin/cysound.drv bin/cykdisk.drv bin/cydisplay.drv bin/cykusb.drv
endif

# Rust userland apps
RUST_APP_SRCS := $(filter-out apps/cykos.rs,$(shell find apps -type f -name '*.rs' | sort))
RUST_APP_BINS := $(foreach s,$(RUST_APP_SRCS),bin/$(shell basename $(s) .rs | tr '[:lower:]' '[:upper:]').BIN)
ifneq ($(wildcard src/user/edit.rs),)
RUST_APP_BINS += bin/EDIT.BIN
endif

# POSIX libc & C userland apps
LIBC_CSRCS := $(wildcard libc/src/*.c)
LIBC_COBJS := $(patsubst libc/src/%.c,bin/libc_objs/%.o,$(LIBC_CSRCS))
ifneq ($(wildcard libc/src/setjmp.S),)
LIBC_SOBJS := bin/libc_objs/setjmp.o
endif
LIBC_OBJS := $(LIBC_COBJS) $(LIBC_SOBJS)

C_APP_SRCS := $(wildcard apps/*.c)
C_APP_BINS := $(foreach s,$(C_APP_SRCS),bin/$(shell basename $(s) .c | tr '[:lower:]' '[:upper:]').BIN)

# Shared libraries (.so)
SO_SRCS := $(wildcard libs/*.c)
SO_BINS := $(foreach s,$(SO_SRCS),bin/LIB$(shell basename $(s) .c | tr '[:lower:]' '[:upper:]').SO)

# Lua objects
LUA_CORES := lapi lcode lctype ldebug ldo ldump lfunc lgc llex lmem lobject \
	lopcodes lparser lstate lstring ltable ltm lundump lvm lzio lauxlib \
	lbaselib lcorolib lstrlib ltablib lutf8lib
LUA_OBJS := $(addprefix bin/lua_objs/,$(addsuffix .o,$(LUA_CORES))) \
	bin/lua_objs/cykos_compat.o bin/lua_objs/main.o

DOOM_DIR := third_party/doomgeneric/doomgeneric
DOOM_OUTPUT := ../../../bin/DOOM.BIN

# Optional outputs
BOOTX64_TARGET := $(if $(HAS_GRUB_MKSTANDALONE),bin/BOOTX64.EFI,)
VDI_TARGET := $(if $(or $(HAS_VBOXMANAGE),$(HAS_QEMU_IMG)),bin/images/cykos.vdi,)
VMDK_TARGET := $(if $(or $(HAS_VBOXMANAGE),$(HAS_QEMU_IMG)),bin/images/cykos.vmdk,)
VHD_TARGET := $(if $(or $(HAS_VBOXMANAGE),$(HAS_QEMU_IMG)),bin/images/cykos.vhd,)
ISO_TARGET := $(if $(HAS_XORRISO),bin/images/cykos-installer.iso,)

ALL_IMAGE_DEPS := \
	bin/mbr.bin bin/stage2.bin bin/KERNEL.BIN bin/KERNEL.ELF $(BOOTX64_TARGET) \
	bin/SHELL.BIN $(DRIVER_BINS) $(SO_BINS) bin/MODULES.CFG \
	$(RUST_APP_BINS) $(C_APP_BINS) bin/LUA.BIN bin/DOOM.BIN bin/DOOM1.WAD

.PHONY: all clean rebuild

all: bin/images/cykos.img $(VDI_TARGET) $(VMDK_TARGET) $(VHD_TARGET) $(ISO_TARGET)
	@echo "----------------------------------------"
	@echo "Build successful! Disk images and, when available, cykos-installer.iso are in bin/images."

clean:
	@echo "Cleaning stale build artifacts..."
	@rm -rf bin
	@if [ -d "$(DOOM_DIR)/build" ]; then rm -rf "$(DOOM_DIR)/build"; fi

rebuild:
	@$(MAKE) clean
	@$(MAKE) all

bin/.dirs-ready:
	@mkdir -p bin bin/libc_objs bin/lua_objs bin/images
	@touch $@

bin/.rust-target-ready: | bin/.dirs-ready
	@if command -v rustup >/dev/null 2>&1; then \
		rustup target add i686-unknown-linux-gnu 2>/dev/null || true; \
	fi
	@touch $@

# Boot assembly targets
bin/boot.o: src/boot/boot.s | bin/.dirs-ready
	nasm -f elf32 $< -o $@

bin/uefi_entry.o: src/boot/uefi_entry.asm | bin/.dirs-ready
	nasm -f elf32 $< -o $@

bin/mbr.bin: src/boot/mbr.asm | bin/.dirs-ready
	nasm -f bin $< -o $@

bin/stage2.bin: src/boot/stage2.asm | bin/.dirs-ready
	nasm -f bin $< -o $@

# Level 1 Kernel & Storage C objects (compiled in parallel)
bin/%.o: src/kernel/%.c | bin/.dirs-ready
	"$(CC)" $(COMMON_CFLAGS) -c $< -o $@

bin/%.o: src/storage/%.c | bin/.dirs-ready
	"$(CC)" $(COMMON_CFLAGS) -c $< -o $@

# Level 3 CykShell (Rust)
bin/libshell.a: src/user/shell.rs bin/.rust-target-ready
	@echo "Compiling Level 3 CykShell (Rust)..."
	@rustc --target i686-unknown-linux-gnu -O -C overflow-checks=no -C relocation-model=static -C panic=abort --crate-type=staticlib $< -o $@

# Link Level 1 Kernel (BIOS flat binary) and UEFI ELF32 Multiboot2 kernel
bin/KERNEL.BIN: $(KERNEL_LINK_OBJS) bin/libshell.a config/linker.ld
	"$(LD)" -m elf_i386 -T config/linker.ld --gc-sections --no-dynamic-linker -no-pie \
		-Map bin/kernel.map $(KERNEL_LINK_OBJS) bin/libshell.a -o $@

bin/kernel.map: bin/KERNEL.BIN
	@true

bin/KERNEL.ELF: bin/uefi_entry.o $(KERNEL_LINK_OBJS) bin/libshell.a config/uefi-linker.ld
	"$(LD)" -m elf_i386 -T config/uefi-linker.ld --gc-sections --no-dynamic-linker -no-pie \
		-Map bin/kernel-uefi.map bin/uefi_entry.o $(KERNEL_LINK_OBJS) bin/libshell.a -o $@
	@if [ "$(HAS_GRUB_FILE)" = "1" ]; then grub-file --is-x86-multiboot2 $@; fi

ifeq ($(HAS_GRUB_MKSTANDALONE),1)
bin/BOOTX64.EFI: tools/grub-uefi.cfg | bin/.dirs-ready
	grub-mkstandalone --format=x86_64-efi --output=$@ \
		--compress=xz --locales='' --fonts='' --themes='' \
		--install-modules="part_msdos fat multiboot2 normal terminal efi_gop video video_fb gfxterm font" \
		--modules="part_msdos fat multiboot2 normal terminal efi_gop video video_fb gfxterm font" \
		"boot/grub/grub.cfg=tools/grub-uefi.cfg"
endif

# Export Kernel Symbols
bin/kernel_syms.ld: bin/kernel.map
	@printf '%s\n' \
		"import re" \
		"symbols = {}" \
		"with open('bin/kernel.map') as f:" \
		"    for line in f:" \
		"        line = line.strip()" \
		"        if not line: continue" \
		"        m = re.search(r'\\b0x([0-9a-fA-F]{4,16})\\s+([a-zA-Z_][a-zA-Z0-9_]*)\\b', line)" \
		"        if m:" \
		"            val = int(m.group(1), 16); name = m.group(2)" \
		"            if 0x10000 <= val < 0x90000 and name not in symbols: symbols[name] = f'0x{val:08x}'" \
		"            continue" \
		"        parts = line.split()" \
		"        if len(parts) >= 2 and re.match(r'^[0-9a-fA-F]{4,16}$$', parts[0]) and re.match(r'^[a-zA-Z_][a-zA-Z0-9_]*$$', parts[-1]):" \
		"            val = int(parts[0], 16); name = parts[-1]" \
		"            if 0x10000 <= val < 0x90000 and name not in symbols: symbols[name] = f'0x{val:08x}'" \
		"with open('$@', 'w') as out:" \
		"    for sym, addr in sorted(symbols.items()): out.write(f'{sym} = {addr};\\n')" \
		"key_syms = [s for s in ['print_string', 'print_char', 'fat16_read_file_by_name', 'fat16_write_file'] if s in symbols]" \
		"print(f'  -> Exported {len(symbols)} kernel symbols. Key symbols found: {\", \".join(key_syms)}')" \
		| python3 -

# Standalone Level 3 Shell
bin/SHELL.BIN: bin/kernel_syms.ld bin/libshell.a src/user/shell.ld
	@python3 -c "with open('bin/kernel_syms.ld') as s, open('bin/kernel_syms_shell.ld', 'w') as t: t.writelines(l for l in s if not l.startswith('shell_main = '))"
	"$(LD)" -m elf_i386 -T src/user/shell.ld --gc-sections -u main bin/kernel_syms_shell.ld bin/libshell.a -o $@
	@rm -f bin/kernel_syms_shell.ld

# Level 2 Standalone Drivers (compiled in parallel)
define DRIVER_RULE
$(word 2,$(subst :, ,$(1))): $(word 1,$(subst :, ,$(1))) Makefile DRIVERS.LST src/kernel/module.h | bin/.dirs-ready
	@mkdir -p "$$(dir $$@)"
	@echo "Building relocatable driver: $$< -> $$@"
	@"$(CC)" $(DRIVER_CFLAGS) -c "$$<" -o "$$@.o"
	@"$(LD)" -m elf_i386 -shared -Bsymbolic --hash-style=sysv --gc-sections -e 0 "$$@.o" -o "$$@"
	@rm -f "$$@.o"
endef
$(foreach p,$(DRIVER_PAIRS),$(eval $(call DRIVER_RULE,$(p))))

# MODULES.CFG from DRIVERS.LST
bin/MODULES.CFG: DRIVERS.LST | bin/.dirs-ready
	@echo "Building MODULES.CFG from DRIVERS.LST manifest..."
	@printf "# CykOS Boot-Time Kernel Module List\n" > $@
	@while IFS= read -r line || [[ -n "$$line" ]]; do \
		[ -z "$$line" ] && continue; \
		echo "$$line" | grep -q '^#' && continue; \
		driver_file=$$(echo "$$line" | awk '{print $$2}'); \
		[ -z "$$driver_file" ] && continue; \
		driver_filename="$${driver_file##*/}"; \
		driver_stem_upper=$$(echo "$${driver_filename%.*}" | tr '[:lower:]' '[:upper:]'); \
		driver_ext_upper=$$(echo "$${driver_filename##*.}" | tr '[:lower:]' '[:upper:]'); \
		driver_file_upper="$${driver_stem_upper:0:8}.$${driver_ext_upper}"; \
		printf "/BOOT/DRIVERS/%s\n" "$$driver_file_upper" >> $@; \
	done < DRIVERS.LST

# Level 4 Rust Userland Applications (compiled in parallel)
define RUST_APP_RULE
$(1): $(2) bin/.rust-target-ready
	@mode="$$$${CYKOS_LINK_MODE:-}"; \
	if [ -z "$$$$mode" ] && [ -f APP_LINKING.LST ]; then \
		mode=$$$$(awk -v app="$(3)" 'toupper($$$$1) == app { print tolower($$$$2); exit }' APP_LINKING.LST); \
	fi; \
	mode="$$$${mode:-static}"; \
	echo "  Compiling Rust App: $(2) -> $(1) ($$$$mode)"; \
	rm -f "$(1)" "$(1).a" "$(1).o"; \
	if rustc $(RUST_FLAGS) --emit=obj "$(2)" -o "$(1).o" 2>/dev/null; then \
		if [ "$$$$mode" = "static" ]; then \
			"$(LD)" -m elf_i386 -T src/user/static_elf.ld --gc-sections --no-dynamic-linker -s -u main "$(1).o" -o "$(1)" 2>/dev/null || rm -f "$(1)"; \
		else \
			"$(LD)" -m elf_i386 -shared -Bsymbolic --hash-style=sysv --gc-sections -e main -s "$(1).o" -o "$(1)" 2>/dev/null || rm -f "$(1)"; \
		fi; \
		rm -f "$(1).o"; \
	fi; \
	if [ ! -f "$(1)" ]; then \
		rustc $(RUST_FLAGS) --crate-type=staticlib "$(2)" -o "$(1).a"; \
		if [ "$$$$mode" = "static" ]; then \
			"$(LD)" -m elf_i386 -T src/user/static_elf.ld --gc-sections --no-dynamic-linker -s -u main "$(1).a" -o "$(1)"; \
		else \
			"$(LD)" -m elf_i386 -shared -Bsymbolic --hash-style=sysv --gc-sections -e main -s "$(1).a" -o "$(1)"; \
		fi; \
		rm -f "$(1).a"; \
	fi
endef

$(foreach s,$(RUST_APP_SRCS),$(eval $(call RUST_APP_RULE,bin/$(shell basename $(s) .rs | tr '[:lower:]' '[:upper:]').BIN,$(s),$(shell basename $(s) .rs | tr '[:lower:]' '[:upper:]'))))
ifneq ($(wildcard src/user/edit.rs),)
$(eval $(call RUST_APP_RULE,bin/EDIT.BIN,src/user/edit.rs,EDIT))
endif

# POSIX C Standard Library (libc.a)
bin/libc_objs/%.o: libc/src/%.c | bin/.dirs-ready
	"$(CC)" $(LIBC_CFLAGS) -c $< -o $@

bin/libc_objs/setjmp.o: libc/src/setjmp.S | bin/.dirs-ready
	"$(CC)" -m32 -ffreestanding -fPIC -c $< -o $@

bin/libc.a: $(LIBC_OBJS)
	"$(AR)" rcs $@ $(LIBC_OBJS)
	@echo "  -> Created: $@"

# Shared Libraries (libs/*.c -> bin/LIB*.SO)
define SO_RULE
$(1): $(2) | bin/.dirs-ready
	@echo "  Building shared library: $(2) -> $(1)"
	@"$(CC)" $(LIBC_CFLAGS) -c "$(2)" -o "$(1).o"
	@"$(LD)" -m elf_i386 -shared --hash-style=sysv -soname "$$(notdir $(1))" "$(1).o" -o "$(1)"
	@rm -f "$(1).o"
endef
$(foreach s,$(SO_SRCS),$(eval $(call SO_RULE,bin/LIB$(shell basename $(s) .c | tr '[:lower:]' '[:upper:]').SO,$(s))))

# C Userland Applications (compiled in parallel after libc.a)
define C_APP_RULE
$(1): $(2) bin/libc.a $(SO_BINS)
	@mode="$$$${CYKOS_LINK_MODE:-}"; \
	if [ -z "$$$$mode" ] && [ -f APP_LINKING.LST ]; then \
		mode=$$$$(awk -v app="$(3)" 'toupper($$$$1) == app { print tolower($$$$2); exit }' APP_LINKING.LST); \
	fi; \
	mode="$$$${mode:-static}"; \
	echo "  Compiling C Userland App: $(2) ($$$$mode)"; \
	"$(CC)" $(LIBC_CFLAGS) -c "$(2)" -o "$(1).o" && \
	if [ "$$$$mode" = "dynamic" ]; then \
		"$(LD)" -m elf_i386 -shared -Bsymbolic --hash-style=sysv --gc-sections -e _start -u _start "$(1).o" bin/libc.a $(SO_BINS) -o "$(1)"; \
	elif [ "$$$$mode" = "high" ]; then \
		"$(LD)" -m elf_i386 -T toolchain/cykos-high.ld -e _start --gc-sections -u _start "$(1).o" bin/libc.a -o "$(1)"; \
	else \
		"$(LD)" -m elf_i386 -T src/user/static_elf.ld -e _start --gc-sections -u _start "$(1).o" bin/libc.a -o "$(1)"; \
	fi && \
	rm -f "$(1).o"
endef
$(foreach s,$(C_APP_SRCS),$(eval $(call C_APP_RULE,bin/$(shell basename $(s) .c | tr '[:lower:]' '[:upper:]').BIN,$(s),$(shell basename $(s) .c | tr '[:lower:]' '[:upper:]'))))

# Lua interpreter (objects compiled in parallel)
bin/lua_objs/%.o: third_party/lua/%.c | bin/.dirs-ready
	"$(CC)" $(LUA_CFLAGS) -c $< -o $@

bin/lua_objs/cykos_compat.o: ports/lua/cykos_compat.c | bin/.dirs-ready
	"$(CC)" $(LUA_CFLAGS) -c $< -o $@

bin/lua_objs/main.o: ports/lua/main.c | bin/.dirs-ready
	"$(CC)" $(LUA_CFLAGS) -c $< -o $@

bin/LUA.BIN: $(LUA_OBJS) bin/libc.a
	"$(LD)" -m elf_i386 -T src/user/static_elf.ld -e _start --gc-sections -u _start $(LUA_OBJS) bin/libc.a -o $@
	@echo "  -> Created: $@ ($$(wc -c < $@) bytes)"

# DoomGeneric (parallelized via jobserver $(MAKE))
bin/DOOM.BIN: bin/libc.a apps/doom/doomgeneric_cykos.c
	@echo "Compiling DoomGeneric for CykOS..."
	@DOOM_OBJS="$$(sed -n 's/^SRC_DOOM = //p' "$(DOOM_DIR)/Makefile" | head -n 1 | tr -d '\r')"; \
	DOOM_OBJS="$${DOOM_OBJS/doomgeneric_xlib.o/doomgeneric_cykos.o}"; \
	$(MAKE) -C "$(DOOM_DIR)" -f Makefile \
		CC="$(CC)" OBJDIR=build OUTPUT="$(DOOM_OUTPUT)" \
		SRC_DOOM="$$DOOM_OBJS" VPATH=../../../apps/doom \
		CFLAGS="-m32 -ffreestanding -Os -fno-builtin -fno-stack-protector -fno-pic -fno-pie -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables -DNORMALUNIX -D_DEFAULT_SOURCE -DCMAP256 -DCYKOS -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200 -I../../../libc/include -I." \
		LDFLAGS="-nostdlib -static -static-libgcc -no-pie -Wl,-m,elf_i386 -Wl,-T,../../../src/user/static_elf.ld -Wl,--gc-sections -Wl,--no-dynamic-linker -Wl,-u,main -Wl,-s" \
		LIBS="../../../bin/libc.a -lgcc"
	@echo "  -> Created $@ ($$(wc -c < $@) bytes)"

bin/DOOM1.WAD: third_party/universal-doom/DOOM1.WAD | bin/.dirs-ready
	cp $< $@

# Disk image packaging
bin/images/os_image.bin: $(ALL_IMAGE_DEPS) makeext2.py install_bios.py
	python3 makeext2.py $@
	python3 install_bios.py $@

bin/images/cykos.img: bin/images/os_image.bin
	cp $< $@

ifeq ($(HAS_QEMU_IMG),1)
bin/images/cykos.vdi: bin/images/os_image.bin
	qemu-img convert -f raw -O vdi $< $@

bin/images/cykos.vmdk: bin/images/os_image.bin
	qemu-img convert -f raw -O vmdk $< $@

bin/images/cykos.vhd: bin/images/os_image.bin
	qemu-img convert -f raw -O vpc $< $@
else ifeq ($(HAS_VBOXMANAGE),1)
bin/images/cykos.vdi: bin/images/os_image.bin
	VBoxManage convertfromraw $< $@ --format VDI

bin/images/cykos.vmdk: bin/images/os_image.bin
	VBoxManage convertfromraw $< $@ --format VMDK

bin/images/cykos.vhd: bin/images/os_image.bin
	VBoxManage convertfromraw $< $@ --format VHD
endif

ifeq ($(HAS_XORRISO),1)
bin/images/cykos-installer.iso: bin/images/os_image.bin bin/KERNEL.ELF tools/build-installer-iso.py
	@echo "Building BIOS/UEFI CD installer ISO..."
	python3 tools/build-installer-iso.py \
		--image bin/images/os_image.bin \
		--kernel-elf bin/KERNEL.ELF \
		--output $@
endif
