# CykOS-x86
The Beginning of my Operating System Development

## Build and run

Build the bootable raw disk and, when the conversion tools are installed, VirtualBox and VMware disk images:

```sh
./make.sh
```

The build produces `bin/os_image.bin`, `bin/cykos.vdi` (with `VBoxManage`), and `bin/cykos.vmdk` (with `qemu-img`). The boot loader uses BIOS EDD LBA reads and E820 memory discovery. The kernel's ATA driver currently supports legacy IDE/ATA PIO, not AHCI or NVMe.

For VirtualBox or VMware, create a 32-bit x86 virtual machine with BIOS/Legacy boot enabled and attach the generated disk as an IDE drive. Do not attach it through AHCI/NVMe unless those drivers are added. Start a configured VM with:

```sh
./run.sh virtualbox "CykOS"
./run.sh vmware /path/to/CykOS.vmx
```

QEMU remains available with `./run.sh qemu`.

The startup memory check uses the BIOS E820 map and tests addressable usable RAM above 1 MiB while saving and restoring each tested page. The disk check reads and compares representative sectors; it does not write to the disk.

## Filesystems

The root filesystem uses a Unix-like layout: `/bin` contains the standalone shell, `/usr/bin` contains apps, `/boot/drivers` and `/boot/MODULES.CFG` hold boot drivers, `/etc` contains command configuration, `/usr/share/doc` contains documentation, `/home/user` is the user home, `/tmp` is for temporary files, `/var/log` is reserved for logs, and `/mnt` is the virtual mount point for additional volumes. Existing `/CYKOS` images and `/drive` paths remain readable for compatibility.

The VFS probes registered filesystem drivers and mounts the first supported boot volume. FAT16 and FAT32 are registered built-in backends. Both support 8.3 path lookup, directory listing/navigation, and file reads; FAT32 also supports file create/replace/delete and empty-directory create/remove. Long file names are skipped. FAT32 is a single-volume implementation and does not yet support FSInfo updates, timestamps, permissions, symbolic links, or crash-safe journaling.

Filesystem code should implement the `vfs_driver_t` operations in `vfs.h` and register its driver in `vfs_mount_root()` in `vfs.c`. Existing FAT16 entry points remain available for compatibility, but kernel, shell, and bundled app I/O use the VFS.

## Syscalls

The `int 0x80` table uses Linux i386 numbers for the implemented POSIX-style subset: `read` (3), `write` (4), `open` (5), `close` (6), `unlink` (10), `chdir` (12), `lseek` (19), `mkdir` (39), `rmdir` (40), `brk` (45), and `getcwd` (183). Syscall number 1 is reserved for `exit`, but process termination semantics are not implemented yet; bundled apps return to the shell through a normal function return. File descriptors are limited, file opens support a subset of access/create/truncate/append flags, and errors return `-1` without a full `errno` implementation. `brk` takes an absolute address; the old increment-based behavior remains as CykOS `sbrk` syscall 112. Legacy CykOS filesystem syscalls 105-111 remain for existing programs. This is not a complete POSIX environment. MS-DOS does not define a universal protected-mode syscall-number ABI, so DOS-specific compatibility is provided only through filesystem/path semantics where they naturally overlap.

## Driver Builds

Add one source/output/load-address row to `DRIVERS.LST`, then run `./drivers/build.sh` after `./make.sh` has generated `bin/kernel_syms.ld`. The main build invokes the same manifest-driven tool. Driver load slots must be 16 KiB aligned within the reserved module region and must not overlap another driver.

## Application Linking

Rust applications are built as position-independent ELF32 shared objects. The kernel's ELF loader maps their load segments at launch, applies i386 REL relocations, and resolves undefined symbols from the explicit kernel export table in `src/kernel/exports.c`. Applications therefore no longer need kernel function addresses baked into their binaries. The loader supports kernel imports and app-local relocations; dependent shared libraries (`DT_NEEDED`) are not supported yet. Older flat binaries continue to load at the legacy app base.

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

Boot CykOS and run `soundtest` (mapped to `/USR/BIN/SOUNDTEST.BIN` in `USERCMDS.LST`) to inspect the active driver's capabilities without starting playback. Use `soundtest chime` or `soundtest tones` to opt in to synchronous playback tests.

### Hardware Limitations

- **PC Speaker:** Modern x64 hardware frequently has no physical PC speaker connected to port 0x61, so the driver loads without error but is silent on real machines.
- **QEMU:** QEMU on WSL/Fedora ARM may not have the `pcspk`/`isa-pcspk` device compiled in. Use `./run.sh qemu` and check for `[run.sh] QEMU audio:` output. If no PC speaker device is available, sound will be silent regardless of driver correctness.
- **HDA Audio:** Detected via PCI but playback is not yet implemented. Full HDA sample playback (DMA buffers, codec init) is planned future work.

CykOS runs in 32-bit protected mode on x64 hardware; audio programming uses legacy x86 I/O ports (PIT 0x42/0x43, speaker 0x61), available in protected mode.

## Graphics

The generic display driver supports text mode (0), VBE 640x480 256-color indexed mode (1), legacy VGA 320x200 256-color indexed mode (2), and VGA 640x480 16-color planar fallback mode (3). Mode 1 uses standard VESA VBE mode 101h with a linear framebuffer and RGB332 DAC palette. If VBE mode 101h with an LFB is unavailable, requesting mode 1 selects the 16-color fallback and returns `-2`; applications can query the active mode with `gfx_get_info` and `gfx_get_mode`. Set mode 3 directly to request the fallback intentionally.

`gfx_get_info` reports the active dimensions, byte pitch, pixel format, and framebuffer pointer. `gfx_framebuffer` returns a pointer for modes 1 and 2. Mode 1 starts with an RGB332 DAC palette; `gfx_set_palette` replaces any palette range using RGB8 triples, so applications can install their own palette and draw a 640x480 8-bit indexed framebuffer without depending on a game-specific interface.

Bitmap drawing accepts pitched `INDEX8`, `RGB332`, `RGB24`, and `RGBA32` data. `INDEX8` uses `0xFF` as transparent; `RGBA32` uses a zero alpha as transparent and blends partial alpha with the destination. Rows are top-to-bottom, pixels left-to-right, and stride is measured in bytes. The graphics syscalls for mode control, display information, bitmap drawing, and palette updates are 187 and 196–199.
