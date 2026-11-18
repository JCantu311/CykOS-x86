# CykOS-x86
The Beginning of my Operating System Development

## Build and run

Build the bootable raw disk and, when conversion tools are installed, virtual disk images:

```sh
./make.sh
```

The build places images under `bin/images`: `os_image.bin` and `cykos.img` are raw images; VDI, VMDK, and VHD are generated when `VBoxManage` or `qemu-img` is installed. The BIOS boot loader reads the FAT16 `KERNEL.BIN` file. The kernel's ATA driver currently supports legacy IDE/ATA PIO, not AHCI or NVMe.

For VirtualBox or VMware, create a 32-bit x86 virtual machine with BIOS/Legacy boot enabled and attach the generated disk as an IDE drive. Do not attach it through AHCI/NVMe unless those drivers are added. Start a configured VM with:

```sh
./run.sh virtualbox "CykOS"
./run.sh vmware /path/to/CykOS.vmx
```

QEMU remains available with `./run.sh qemu`.

Startup reports usable RAM from the BIOS E820 map without writing to arbitrary physical addresses; the write test is skipped for firmware and emulator compatibility. The disk check reads and compares representative sectors; it does not write to the disk.

## Filesystems

The root filesystem uses a Unix-like layout: `/bin` contains the standalone shell, `/usr/bin` contains apps, `/boot/drivers` and `/boot/MODULES.CFG` hold boot drivers, `/etc` contains command configuration, `/usr/share/doc` contains documentation, `/home/user` is the user home, `/tmp` is for temporary files, `/var/log` is reserved for logs, and `/mnt` is the virtual mount point for additional volumes. Existing `/CYKOS` images and `/drive` paths remain readable for compatibility.

CykShell's Tab completion resolves command names in the first token and file or directory names in later tokens. It uses the mounted VFS directory listing, compares names case-insensitively, appends `/` for a directory, and adds a space after a completed file.

CykShell reads the optional `/ETC/AUTOEXEC.SHE` file after its startup banner and before showing the first prompt. It uses the existing `.SHE` script parser: one normal shell command per line, with `#` comments. The image builder installs the repository's `AUTOEXEC.SHE` template at that path; edit the template to select startup commands.

The VFS probes registered filesystem drivers and mounts the first supported boot volume. FAT16 and FAT32 are registered built-in backends. Both support 8.3 path lookup, directory listing/navigation, and file reads; FAT32 also supports file create/replace/delete and empty-directory create/remove. Long file names are skipped. FAT32 is a single-volume implementation and does not yet support FSInfo updates, timestamps, permissions, symbolic links, or crash-safe journaling.

Filesystem code should implement the `vfs_driver_t` operations in `vfs.h` and register its driver in `vfs_mount_root()` in `vfs.c`. Existing FAT16 entry points remain available for compatibility, but kernel, shell, and bundled app I/O use the VFS.

## Syscalls

The `int 0x80` table uses Linux i386 numbers for the implemented POSIX-style subset: `exit` (1), `read` (3), `write` (4), `open` (5), `close` (6), `wait` (7), `unlink` (10), `chdir` (12), `lseek` (19), `mkdir` (39), `rmdir` (40), `brk` (45), and `getcwd` (183). Applications run synchronously from the shell with a private 128 KiB stack. Kernel heap allocations, app `brk`/`sbrk` storage, and open file descriptors are tracked to the active process and reclaimed when it returns, calls `exit`, is interrupted with Ctrl+C, or faults; the app heap arena is capped at 8 MiB. A small process record tracks its PID and exit status; the shell's `wait` command collects the most recent completed app's PID and status. Apps still execute at ring 0 without address-space isolation, so exception recovery cannot contain arbitrary memory corruption. File descriptors are limited, file opens support a subset of access/create/truncate/append flags, and errors return `-1` without a full `errno` implementation. `brk` takes an absolute address; the old increment-based behavior remains as CykOS `sbrk` syscall 112. Legacy CykOS filesystem syscalls 105-111 remain for existing programs. This is not a complete POSIX environment. MS-DOS does not define a universal protected-mode syscall-number ABI, so DOS-specific compatibility is provided only through filesystem/path semantics where they naturally overlap.

## Driver Builds

Add one source/output/load-address row to `DRIVERS.LST`, then run `./drivers/build.sh` after `./make.sh` has generated `bin/kernel_syms.ld`. The main build invokes the same manifest-driven tool. Driver load slots must be 16 KiB aligned within the reserved module region and must not overlap another driver.

## Application Linking

Applications can use dynamic or static linking. Set each program's mode in `APP_LINKING.LST` (`APP_NAME dynamic|static`); bundled apps default to static, and `CYKOS_LINK_MODE=static|dynamic` can override the manifest for a build. The kernel identifies ELF `ET_DYN` and `ET_EXEC` files from their headers; existing flat static binaries remain supported. Dynamic ELF32 applications use i386 REL relocations and kernel imports from `src/kernel/exports.c`. Dependent shared libraries (`DT_NEEDED`) are not supported yet. Static Rust apps are linked against `bin/kernel_syms.ld` and emitted as `ET_EXEC`.

The initial Lua port builds the upstream Lua core as a CykOS command. It runs text scripts, supports `-e` and a small interactive prompt, and currently enables the base, coroutine, string, table, and UTF-8 libraries. See `ports/lua/README.md` for current library and runtime limitations.

## Sound

CykOS uses a **modular, auto-detecting sound subsystem** designed for game porting (e.g., DoomGeneric-style audio):

### Driver Architecture

The sound subsystem loads multiple Level 2 driver modules and auto-selects the best one based on capability bitmask (`SOUND_CAP_*`):

| Driver | Source | Load Address | Capabilities |
|--------|--------|--------------|--------------|
| PC Speaker | `src/drivers/pcsounddrv.c` | `0x50000` | TONE, CHIME, VOLUME |
| HDA Audio | `src/drivers/hdasound.c` | `0x54000` | Detection only (playback TBD) |

- **Driver selection:** `modload` loads all sound drivers from `/BOOT/MODULES.CFG`, calls each driver's `get_caps()`, and auto-selects the one with the highest capability score.
- **Manual override:** Applications can enumerate all drivers via `modload_get_all_sound_drivers()` and select a specific one via `modload_select_sound_driver()`.
- **Kernel fallback:** If no driver is loaded or a driver doesn't support a feature, the kernel falls back to direct PC speaker programming (PIT channel 2 + port 0x61).

### Syscalls (`int 0x80`)

| Syscall | Number | Description |
|---------|--------|-------------|
| `SYS_SOUND_TONE` | 184 | `sound_tone(freq, ms)` — play a tone |
| `SYS_SOUND_CHIME` | 185 | `sound_chime()` — play alert melody |
| `SYS_SOUND_SET_VOLUME` | 192 | `sound_set_volume(volume)` — set volume 0-100 |
| `SYS_SOUND_PLAY_SAMPLE` | 193 | `sound_play_sample(data, len, freq_hz)` — play digitized sample |
| `SYS_SOUND_STOP` | 194 | `sound_stop()` — stop all audio |
| `SYS_SOUND_GET_CAPS` | 195 | `sound_get_caps()` — query driver capabilities |

### Sound Capability Flags (`SOUND_CAP_*`)

| Flag | Value | Meaning |
|------|-------|---------|
| `SOUND_CAP_NONE` | 0x00 | No audio capability |
| `SOUND_CAP_TONE` | 0x01 | Can play tone(freq, ms) |
| `SOUND_CAP_CHIME` | 0x02 | Can play chime melody |
| `SOUND_CAP_SAMPLE` | 0x04 | Can play digitized samples |
| `SOUND_CAP_MUSIC` | 0x08 | Can play background music |
| `SOUND_CAP_VOLUME` | 0x10 | Volume control supported |
| `SOUND_CAP_ALL` | 0xFF | All capabilities |

### Userland API (Rust `apps/cykos.rs`)

```rust
cykos::sound_tone(freq, ms)        -> i32
cykos::sound_chime()                -> i32
cykos::sound_set_volume(volume)     -> i32  // 0-100
cykos::sound_play_sample(data, len, freq_hz) -> i32
cykos::sound_stop()                 -> i32
cykos::sound_get_caps()             -> u32  // SOUND_CAP_* bitmask
```

Used by `apps/soundtest.rs` and `apps/games/cykis.rs`. Suitable for porting DoomGeneric and similar software.

### Testing

Test and demonstration binaries are installed under `/USR/BIN/TESTS` and are not added to the normal command aliases. For example, run `/USR/BIN/TESTS/SOUNDTEST.BIN` to inspect audio capabilities, or `/USR/BIN/TESTS/GFXDEMO.BIN` to inspect VGA graphics.

### Hardware Limitations

- **PC Speaker:** Modern x64 hardware frequently has no physical PC speaker connected to port 0x61, so the driver loads without error but is silent on real machines.
- **QEMU:** QEMU on WSL/Fedora ARM may not have the `pcspk`/`isa-pcspk` device compiled in. Use `./run.sh qemu` and check for `[run.sh] QEMU audio:` output. If no PC speaker device is available, sound will be silent regardless of driver correctness.
- **HDA Audio:** Detected via PCI but playback is not yet implemented. Full HDA sample playback (DMA buffers, codec init) is planned future work.

CykOS runs in 32-bit protected mode on x64 hardware; audio programming uses legacy x86 I/O ports (PIT 0x42/0x43, speaker 0x61), available in protected mode.

## Graphics

The generic display driver uses standard VGA modes only: text mode (0), 640x480 16-color planar mode 12h (1), and 320x200 256-color mode 13h (2). Mode 3 remains accepted as an alias for mode 1 for compatibility. The 640x480 mode uses the VGA planar aperture and exposes no linear framebuffer; the 320x200 mode exposes an 8-bit indexed framebuffer.

`gfx_get_info` reports the active dimensions, byte pitch, pixel format, and framebuffer pointer. `gfx_framebuffer` returns a pointer for mode 2; mode 1 is planar and has no linear framebuffer. `gfx_set_palette` can replace 16 VGA palette entries in mode 1 or all 256 DAC entries in mode 2 using RGB8 triples.

Bitmap drawing accepts pitched `INDEX8`, `RGB332`, `RGB24`, and `RGBA32` data. `INDEX8` uses `0xFF` as transparent; `RGBA32` uses a zero alpha as transparent and blends partial alpha with the destination. Rows are top-to-bottom, pixels left-to-right, and stride is measured in bytes. The graphics syscalls for mode control, display information, bitmap drawing, and palette updates are 187 and 196–199.

## DoomGeneric

The `doom` command builds the stock DoomGeneric sources with its upstream Makefile, using a separate CykOS platform shim, the CykOS libc, and a static i386 link. The fixed application arena, shell stack, foreground app stack, and guard gap have separate memory ranges; the kernel heap can grow from 0x290000 up to 16 MiB, limited by contiguous usable RAM reported by BIOS E820 (with an 8 MiB fallback). Doom uses VGA mode 13h (320x200 indexed color), the CykOS graphics, keyboard, and millisecond-sleep syscalls. The shareware `DOOM1.WAD` is bundled under `/USR/SHARE/GAMES/DOOM`; the CykOS platform shim supplies that path by default. Run `doom -iwad /path/to/game.wad` to select another compatible IWAD. The build is driven by `apps/doom/build.sh` and is also part of `make.sh`.
