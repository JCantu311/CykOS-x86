# CykOS-x86
The Beginning of my Operating System Development

## Build and run

Build the bootable raw disk and, when conversion tools are installed, virtual disk images:

```sh
./make.sh
```

The build places images under `bin/images`: `os_image.bin` and `cykos.img` are raw images; VDI, VMDK, and VHD are generated when `VBoxManage` or `qemu-img` is installed. BIOS boots `KERNEL.BIN` through the FAT16 loader. UEFI boots `EFI/BOOT/BOOTX64.EFI`, which starts the Multiboot2 ELF kernel. The kernel probes native AHCI SATA, NVMe namespace 1, then legacy IDE PIO for its root disk.

When `xorriso` is installed, `./make.sh` also creates
`bin/images/cykos-installer.iso` with BIOS and UEFI El Torito entries. In
BIOS/Legacy mode the menu offers a CykOS live session or disk installation. In
UEFI mode GRUB starts the live kernel and passes the bundled disk image as a
Multiboot module; the live session mounts its ext2 root. With enough contiguous
RAM it copies the image to a RAM disk, so filesystem changes work during the
session and disappear at shutdown. The UEFI entry currently boots the live
session; the destructive disk-install menu is BIOS-only.

The BIOS install option lists BIOS hard disks, excludes the BIOS boot medium, and
requires selecting a disk plus typing `ERASE` before writing. It replaces the
selected disk's partition table and writes the bundled image from sector 0;
sectors beyond the image on a larger disk are left unchanged. The target must
be at least as large as the image and expose 512-byte sectors through BIOS
EDD. The installer ISO builder verifies that its embedded disk image and BIOS
kernel match the source files. UEFI boot files require `grub-mkstandalone`,
GRUB EFI modules, and mtools (`mformat`, `mmd`, and `mcopy`).
If the ISO is not generated, install `xorriso` and rebuild.

## Disk image utility

`tools/cykos-disk.py` provides host-side operations for the current BIOS image
layout. After building CykOS, create an image with
`python3 tools/cykos-disk.py format-image /path/to/cykos.img` and inspect its
partition table with `python3 tools/cykos-disk.py inspect /path/to/cykos.img`.
`format-image` refuses to replace an existing file unless `--force` is given.
These commands operate on image files only; they do not partition or format a
physical disk. The current layout is an MBR table with an EFI System/FAT16
boot partition and an ext2 root partition. UEFI boot uses the removable-media
fallback path on the FAT partition; GPT and native in-OS disk management are
not implemented. Creating UEFI boot files requires `grub-mkstandalone` and
the GRUB x86_64 EFI modules. The polled AHCI and NVMe paths use DMA below
4 GiB and support 512-byte logical sectors; NVMe currently exposes namespace
1 through the first detected controller. Legacy IDE remains available as a
fallback.

Run `python3 -m unittest discover -s tests -v` for host-side image-layout
regression checks, `python3 tools/cykos-disk.py validate bin/images/cykos.img`
to check the MBR, partition bounds, overlap, FAT16 label, and ext superblock,
and `python3 tools/verify-image.py bin/images/cykos.img` to run `e2fsck -fn`
on an extracted copy of the root partition. These checks do not replace
booting the image in an emulator.

For VirtualBox or VMware, create a 32-bit x86 virtual machine with BIOS/Legacy boot enabled and attach the generated disk as IDE, AHCI, or NVMe storage. To use UEFI, enable EFI firmware and boot the generated disk's EFI fallback loader. Start a configured VM with:

```sh
./run.sh virtualbox "CykOS"
./run.sh vmware /path/to/CykOS.vmx
```

QEMU remains available with `./run.sh qemu`.

Startup reports usable RAM from the BIOS E820 map without writing to arbitrary physical addresses; the write test is skipped for firmware and emulator compatibility. The disk check reads and compares representative sectors; it does not write to the disk.

The flat standalone shell has a reserved physical-memory window configured in `config/memory_layout.ld`. Both the shell linker script and kernel loader use linker symbols for the window's base and limit, and the linker rejects a shell image that exceeds it. The reservation is between the shell/app stacks and the dynamic-app arena, separate from the low-memory driver slots and fixed-address app image region.

## Filesystems

The root filesystem uses a Unix-like layout: `/bin` contains the standalone shell, `/usr/bin` contains apps, `/boot/drivers` and `/boot/MODULES.CFG` hold boot drivers, `/etc` contains command configuration, `/usr/share/doc` contains documentation, `/home/user` is the user home, `/tmp` is for temporary files, `/var/log` is reserved for logs, and `/mnt` is the virtual mount point for additional volumes. Existing `/CYKOS` images and `/drive` paths remain readable for compatibility.

CykShell's Tab completion resolves command names in the first token and file or directory names in later tokens. It uses the mounted VFS directory listing, compares names case-insensitively, appends `/` for a directory, and adds a space after a completed file. Ctrl+L clears the terminal and redraws the prompt and any command line currently being edited.

CykShell reads the optional `/ETC/AUTOEXEC.SHE` file after its startup banner and before showing the first prompt. It uses the existing `.SHE` script parser: one normal shell command per line, with `#` comments. The image builder installs the repository's `AUTOEXEC.SHE` template at that path; edit the template to select startup commands.

CykShell supports up to eight commands in a pipeline (`cat file | grep text`), conditional command lists (`cmd && next` and `cmd || fallback`), and output redirection (`echo hello > file`, `cat file >> log`). `&&` and `||` use whether the command was found as their current success signal; built-in command failure statuses and application exit codes are not yet propagated. Added text tools include `echo`, `grep`, `head`, and `tail`; `grep` performs a literal, case-sensitive substring match, and `head`/`tail` show ten lines.

The VFS probes registered filesystem drivers and mounts the first supported volume. The ext2 backend is registered before FAT16/FAT32 and probes MBR Linux partitions (type `0x83`); it supports 1 KiB blocks, ordinary ext2 directory entries, sparse-file reads, and direct, single-indirect, and double-indirect file blocks. It supports file creation, whole-file replacement, deletion, directory creation, and removal of empty directories. Whole-file replacement writes a new inode and switches the directory entry after the new contents are ready; failed cleanup places the mount into a write-protected state. This reduces exposure to interrupted rewrites and reports later writes as failures. It does not provide power-loss atomicity: ext2 metadata updates have no journal or recovery pass. The driver uses compatibility case-insensitive path lookup and rejects unsupported feature flags. The image builder creates an 80 MiB disk with a FAT16 BIOS boot partition containing only `KERNEL.BIN` and a separate ext2 root partition containing the complete OS tree. The bootloader still loads the kernel from FAT16; after startup, ext2 provides `/`. FAT16 root mounting remains as a legacy fallback for older disk images. Triple-indirect files are unsupported. Building the image requires `mke2fs` and `debugfs` from e2fsprogs.

Filesystem code should implement the `vfs_driver_t` operations in `vfs.h` and register its driver in `vfs_mount_root()` in `vfs.c`. Existing FAT16 entry points remain available for compatibility, but kernel, shell, and bundled app I/O use the VFS.

## Syscalls

The `int 0x80` table uses Linux i386 numbers for the implemented POSIX-style subset: `exit` (1), `read` (3), `write` (4), `open` (5), `close` (6), `wait` (7), `unlink` (10), `chmod` (15), `getpid` (20), `setuid` (23), `getuid` (24), `access` (33), `chdir` (12), `lseek` (19), `mkdir` (39), `rmdir` (40), `setgid` (46), `getgid` (47), `umask` (60), `brk` (45), `chown` (182), and `getcwd` (183). Static ELF applications run at ring 3 with a private 256 KiB stack and an app heap capped at 16 MiB, further limited by contiguous usable RAM reported by BIOS E820. Kernel heap allocations, app `brk`/`sbrk` storage, and open file descriptors are tracked to the active process and reclaimed when it returns, calls `exit`, is interrupted with Ctrl+C, or faults. A small process record tracks its PID and exit status; the shell's `wait` command collects the most recent completed app's PID and status. CykOS currently runs one foreground application at a time and has no scheduler or concurrent processes. User memory is selectively enabled for the active ring 3 app and revoked during cleanup; this remains a shared identity-mapped address space rather than per-process virtual memory. UID/GID/umask state is inherited by the foreground app and restored to the shell when it exits. There is no login or multiuser session manager, and file permission records do not provide a security boundary against ring 0 compatibility applications. Legacy flat binaries and dynamic applications with kernel imports retain the ring 0 compatibility path. File descriptors are limited, file opens support a subset of access/create/truncate/append flags, and errors return `-1` without a full `errno` implementation. `brk` takes an absolute address; the old increment-based behavior remains as CykOS `sbrk` syscall 112. Legacy CykOS filesystem syscalls 105-111 remain for existing programs. This is not a complete POSIX environment. MS-DOS does not define a universal protected-mode syscall-number ABI, so DOS-specific compatibility is provided only through filesystem/path semantics where they naturally overlap.

## File permissions

CykOS applies numeric UID/GID and Unix-style `rwx` mode checks at the VFS
boundary, including shell reads, writes, listings, directory changes, and
deletes. Paths are normalized before permission records are looked up. The
persistent `/ETC/PERMISSIONS.DB` stores up to 128 explicit path records; files
created through CykOS receive the current UID/GID and the active umask. Files
without an explicit record default to root ownership, directories to `0755`,
ordinary files to `0644`, and `.BIN`, `.COM`, and `.SHE` files to `0755`.
Deleted paths lose their permission record. If the permission database cannot
be saved, CykOS reports the operation as failed and denies non-root access
until reboot so unsaved metadata cannot silently authorize later operations.

The shell provides `id`, `whoami`, `users`, `stat <path>`, `access <rwx>
<path>`, `chmod <octal-mode> <path>`, `chown <uid[:gid]> <path>`, and `umask
[octal-mask]`. Root creates up to 32 accounts with `useradd <name> <uid> <gid>
[sudo]`; names use letters, digits, `_`, and `-`, and each UID must be unique.
`usermod <name> sudo|nosudo` changes sudo authorization, and `userdel <name>`
removes an account (numeric file ownership is left unchanged).
Set passwords with `passwd [name]`; password input is not echoed. Password
verifiers are stored in root-only `/ETC/ACCOUNTS.DB` using salted, iterated
SHA-256. Set an initial root password with `passwd root`. `su [name]` switches
the shell identity after password authentication (root can switch without a
password). `sudo <command> [arguments]` runs a shell command as root after
authentication for accounts created with the optional `sudo` flag. `runas
<uid> <gid> <app> [args]` remains available to UID 0 for numeric app launches.
There is no boot login manager, home-directory provisioning, supplementary
group list, symbolic `chmod` syntax, or session isolation; the boot console
starts as root. Root bypasses read and write mode bits; execution still
requires at least one execute bit.

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

- **Keyboard:** The loadable keyboard module retains the kernel's IRQ1/i8042
  PS/2 input path. USB HID boot keyboards are added through `CYKUSB.DRV` and
  feed the same keyboard event queue.
- **USB:** The loadable `CYKUSB.DRV` module provides UHCI, OHCI, EHCI, and xHCI
  host controllers for directly connected USB HID boot keyboards and USB mass-storage devices using the Bulk
  Only Transport protocol, including devices with multiple logical units. FAT16
  USB volumes can be mounted as extra drives.
  USB hubs, hot-plug discovery, non-boot HID devices, UAS storage, and USB
  booting are not implemented. The xHCI backend currently initializes one
  controller.
- **PC Speaker:** Modern x64 hardware frequently has no physical PC speaker connected to port 0x61, so the driver loads without error but is silent on real machines.
- **QEMU:** QEMU on WSL/Fedora ARM may not have the `pcspk`/`isa-pcspk` device compiled in. Use `./run.sh qemu` and check for `[run.sh] QEMU audio:` output. If no PC speaker device is available, sound will be silent regardless of driver correctness.
- **HDA Audio:** Detected via PCI but playback is not yet implemented. Full HDA sample playback (DMA buffers, codec init) is planned future work.

The current hardware target is 32-bit x86 BIOS or GRUB Multiboot2 UEFI boot in
QEMU and compatible PCs. Storage support is IDE PIO, polled AHCI, and NVMe
namespace 1 on the first discovered controller. USB support is limited to the
documented UHCI/OHCI/EHCI/xHCI boot-keyboard and BOT mass-storage paths. HDA
playback, USB hubs and hot-plug, UAS, USB boot, GPT, and native partition
management are outside the implemented support set.

CykOS runs in 32-bit protected mode on x64 hardware; audio programming uses legacy x86 I/O ports (PIT 0x42/0x43, speaker 0x61), available in protected mode.

## Graphics

The generic display driver uses standard VGA modes only: text mode (0), 640x480 16-color planar mode 12h (1), and 320x200 256-color mode 13h (2). Mode 3 remains accepted as an alias for mode 1 for compatibility. The 640x480 mode uses the VGA planar aperture and exposes no linear framebuffer; the 320x200 mode exposes an 8-bit indexed framebuffer.

`gfx_get_info` reports the active dimensions, byte pitch, pixel format, and framebuffer pointer. `gfx_framebuffer` returns a pointer for mode 2; mode 1 is planar and has no linear framebuffer. `gfx_set_palette` can replace 16 VGA palette entries in mode 1 or all 256 DAC entries in mode 2 using RGB8 triples.

Bitmap drawing accepts pitched `INDEX8`, `RGB332`, `RGB24`, and `RGBA32` data. `INDEX8` uses `0xFF` as transparent; `RGBA32` uses a zero alpha as transparent and blends partial alpha with the destination. Rows are top-to-bottom, pixels left-to-right, and stride is measured in bytes. The graphics syscalls for mode control, display information, bitmap drawing, and palette updates are 187 and 196–199.

## DoomGeneric

The `doom` command builds the stock DoomGeneric sources with its upstream Makefile, using a separate CykOS platform shim, the CykOS libc, and a static i386 link. Doom runs in ring 3 with its own stack and a bounded heap backed by contiguous RAM reported by BIOS E820. Its framebuffer output uses the bitmap syscall rather than a direct VGA pointer. The shareware `DOOM1.WAD` is bundled under `/USR/SHARE/GAMES/DOOM`; the CykOS platform shim supplies that path by default. Run `doom -iwad /path/to/game.wad` to select another compatible IWAD. The build is driven by `apps/doom/build.sh` and is also part of `make.sh`.
