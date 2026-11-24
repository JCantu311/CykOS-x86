# CykOS smoke test

Run this after changes to the boot path, installer, storage, syscall ABI, or
application loader. It is a short manual regression pass for the currently
supported BIOS/IDE configuration.

## Build and inspect artifacts

1. Run `./make.sh` from the repository root.
2. Confirm the build completes and produces `bin/images/cykos.img`.
3. If `xorriso` is installed, confirm `bin/images/cykos-installer.iso` is
   produced too.
4. Run `python3 tools/cykos-disk.py inspect bin/images/cykos.img`. Check for
   an MBR signature, a bootable FAT16 partition, and an ext2 partition with
   the expected filesystem magic.

## Boot the disk image

1. Run `./run.sh qemu` with the default IDE disk and BIOS boot settings.
2. Confirm startup reaches CykShell without a kernel panic and reports usable
   memory and the mounted root filesystem.
3. Run `help`, `dir /`, `cat /ETC/USERCMDS.LST`, and `wait`. `wait` should
   report that there is no exited app when no app has run yet.
4. Run `/USR/BIN/TESTS/SYSDEMO.BIN` or
   `/USR/BIN/TESTS/GFXDEMO.BIN`, return to the shell, then run `wait`. Confirm
   it reports one PID and the app's exit status. Run `wait` again and confirm
   the process was reaped.
5. Run a file app check (for example `edit /TMP/smoke.txt`, save, then `cat
   /TMP/smoke.txt`) to exercise file syscalls and shell recovery after an app.
6. If available, repeat once with `CYKOS_QEMU_USB_KBD=1 ./run.sh qemu` and
   type shell commands from the USB keyboard.

## UEFI, AHCI, and NVMe

These paths require a fresh image built with `grub-mkstandalone` available.
Use a disposable copy of the image for each boot test.

1. Boot `bin/images/cykos.img` with UEFI firmware and confirm GRUB starts
   `KERNEL.ELF`, CykOS mounts ext2, and the shell runs. On QEMU, use OVMF
   firmware and a Q35 machine; firmware paths vary by host distribution.
2. For AHCI, attach the disk to an `ich9-ahci` controller and confirm startup
   reports the AHCI disk before the non-destructive sector check and ext2
   mount.
3. For NVMe, attach the disk with QEMU's `nvme` device and confirm startup
   reports namespace 1, passes the sector check, and mounts ext2.
4. On both native storage paths, run an app and create/read a file under
   `/TMP` to cover writes and flush behavior.

## Boot the installer ISO

1. Boot `bin/images/cykos-installer.iso` in a BIOS-mode VM with an IDE ATAPI
   CD device. Confirm the menu offers live and install choices.
2. Choose the live session. Confirm it mounts the bundled ext2 root and
   reaches CykShell. On a RAM-disk path, create a file under `/TMP`; on the
   low-memory read-only path, confirm a write fails cleanly.
3. Restart and choose install only with a disposable blank VM disk attached.
   Confirm the target disk is listed, the typed confirmation is required, and
   the installed disk boots to CykShell after detaching the ISO.

## Recovery checks

Run these in the disposable QEMU VM after the normal smoke pass:

1. Run an app that intentionally faults (if a fault-test binary is available).
   Confirm the kernel reports the exception and returns to CykShell.
2. Run `wait` and confirm the faulted app has a nonzero status.
3. Run another normal app and use `wait` again. This checks that app mappings,
   descriptors, heap ownership, graphics mode, and keyboard state were reset.

The installer test overwrites its selected disk. Use a disposable VM disk, not
a host disk. UEFI, AHCI/NVMe, USB hubs, hot-plug, and virtual CD paths without
IDE ATAPI exposure are outside this smoke test's supported configuration.
