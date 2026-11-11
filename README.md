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
