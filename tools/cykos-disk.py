#!/usr/bin/env python3
"""Safe host-side image tools for the current CykOS BIOS/ext2 disk layout.

This intentionally operates on regular image files only. It does not format
physical disks. The on-disk layout is currently maintained by makeext2.py.
"""

import argparse
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)


def inspect(path):
    with open(path, "rb") as image:
        mbr = image.read(512)
        if len(mbr) != 512:
            raise ValueError("image is smaller than one sector")
        if mbr[510:512] != b"\x55\xaa":
            raise ValueError("missing MBR signature")
        print(f"Image: {path} ({os.path.getsize(path)} bytes)")
        print("Partition table: MBR")
        for index in range(4):
            entry = mbr[446 + index * 16:462 + index * 16]
            boot, _chs0, kind, _chs1, start, count = struct.unpack("<B3sB3sII", entry)
            if not kind and not start and not count:
                continue
            print(f"  {index + 1}: type=0x{kind:02x} start={start} sectors={count}"
                  f" bootable={'yes' if boot == 0x80 else 'no'}")
            if kind in (0x06, 0x0E, 0xEF) and start and count:
                image.seek(start * 512 + 54)
                print(f"     FAT type label: {image.read(8).decode('ascii', 'replace').strip()}")
            if kind == 0x83 and start and count:
                image.seek(start * 512 + 1024 + 56)
                magic = image.read(2)
                ext_magic = "0xef53 (ext)" if magic == bytes((0x53, 0xef)) else "unknown"
                print(f"     ext filesystem magic: {ext_magic}")


def format_image(path, kernel_path, force):
    if os.path.exists(path) and not os.path.isfile(path):
        raise ValueError("target must be a regular image file; physical devices are not supported")
    if os.path.exists(path) and not force:
        raise ValueError(f"{path} already exists; pass --force to replace this image")
    if not os.path.isfile(kernel_path):
        raise ValueError(f"kernel image not found: {kernel_path}")
    if os.path.realpath(path) == os.path.realpath(kernel_path):
        raise ValueError("image path must differ from kernel path")

    mbr_path = os.path.join(ROOT, "bin", "mbr.bin")
    stage2_path = os.path.join(ROOT, "bin", "stage2.bin")
    if not os.path.isfile(mbr_path) or not os.path.isfile(stage2_path):
        raise ValueError("BIOS boot binaries missing; build CykOS before installing an image")
    with open(mbr_path, "rb") as stream:
        mbr = stream.read()
    with open(stage2_path, "rb") as stream:
        stage2 = stream.read()
    if len(mbr) != 512 or len(stage2) > 16 * 512:
        raise ValueError("BIOS boot binaries have invalid sizes")

    try:
        from makeext2 import format_disk
    except ImportError as exc:
        raise ValueError(f"cannot load repository image formatter: {exc}") from exc

    with open(kernel_path, "rb") as kernel_file:
        kernel = kernel_file.read()
    files = {
        "KERNEL.BIN": kernel,
        "BOOT/MODULES.CFG": "# CykOS driver modules\n",
        "ETC/USERCMDS.LST": "# External commands\n",
        "ETC/AUTOEXEC.SHE": "# CykShell startup commands\n",
        "VAR/LOG/LOG.TXT": "",
        "USR/SHARE/DOC/README.TXT": "CykOS ext2 root filesystem.\n",
    }
    directories = ["BIN", "BOOT/DRIVERS", "ETC", "HOME/USER", "MNT", "TMP",
                   "USR/BIN", "USR/SHARE/DOC", "VAR/LOG"]
    format_disk(path, files, directories)

    with open(path, "r+b") as image:
        image.seek(0)
        image.write(mbr[:446])  # preserve the MBR partition table
        image.seek(512)
        image.write(stage2.ljust(16 * 512, b"\0"))
    print(f"Installed BIOS/ext2 CykOS image: {path}")
    print("Layout: BIOS MBR + FAT16 loader volume + ext2 root volume")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    show = commands.add_parser("inspect", help="show the MBR partition layout")
    show.add_argument("image")
    create = commands.add_parser("format-image", help="create a bootable CykOS image file")
    create.add_argument("image")
    create.add_argument("--kernel", default=os.path.join(ROOT, "bin", "KERNEL.BIN"))
    create.add_argument("--force", action="store_true", help="replace an existing image file")
    args = parser.parse_args()
    try:
        if args.command == "inspect":
            inspect(args.image)
        else:
            format_image(args.image, args.kernel, args.force)
    except (OSError, ValueError, RuntimeError) as exc:
        parser.error(str(exc))


if __name__ == "__main__":
    main()
