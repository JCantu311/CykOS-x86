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
    layout = validate_image(path)
    print(f"Image: {path} ({layout['size']} bytes)")
    print("Partition table: MBR")
    for index, part in layout["partitions"]:
        print(f"  {index + 1}: type=0x{part['type']:02x} start={part['start']} sectors={part['sectors']}"
              f" bootable={'yes' if part['boot'] == 0x80 else 'no'}")
        if part["type"] in (0x06, 0x0E, 0xEF):
            print("     FAT type label: FAT16")
        if part["type"] == 0x83:
            print("     ext filesystem magic: 0xef53 (ext)")
    print("Validation: bootable FAT16 and ext2 root partitions are in bounds and do not overlap.")


def validate_image(path):
    with open(path, "rb") as image:
        size = os.fstat(image.fileno()).st_size
        mbr = image.read(512)
        if len(mbr) != 512:
            raise ValueError("image is smaller than one sector")
        if mbr[510:512] != b"\x55\xaa":
            raise ValueError("missing MBR signature")
        total_sectors = size // 512
        partitions = []
        for index in range(4):
            entry = mbr[446 + index * 16:462 + index * 16]
            boot, _chs0, kind, _chs1, start, count = struct.unpack("<B3sB3sII", entry)
            if not kind and not start and not count:
                continue
            if boot not in (0, 0x80):
                raise ValueError(f"partition {index + 1} has invalid boot flag 0x{boot:02x}")
            if not kind or not start or not count:
                raise ValueError(f"partition {index + 1} has an incomplete entry")
            if start >= total_sectors or count > total_sectors - start:
                raise ValueError(f"partition {index + 1} extends beyond the image")
            partitions.append((index, {"boot": boot, "type": kind, "start": start,
                                       "sectors": count}))

        ordered = sorted(partitions, key=lambda item: item[1]["start"])
        for previous, current in zip(ordered, ordered[1:]):
            prev_end = previous[1]["start"] + previous[1]["sectors"]
            if prev_end > current[1]["start"]:
                raise ValueError(f"partitions {previous[0] + 1} and {current[0] + 1} overlap")

        fat = [part for _, part in partitions if part["type"] in (0x06, 0x0E, 0xEF)]
        ext = [part for _, part in partitions if part["type"] == 0x83]
        if len(fat) != 1 or fat[0]["boot"] != 0x80:
            raise ValueError("expected exactly one bootable FAT16 partition (type 0x06, 0x0e, or 0xef)")
        if len(ext) != 1:
            raise ValueError("expected exactly one ext2 root partition (type 0x83)")

        image.seek(fat[0]["start"] * 512 + 54)
        if image.read(5) != b"FAT16":
            raise ValueError("boot partition is not labeled FAT16")
        image.seek(ext[0]["start"] * 512 + 1024 + 56)
        if image.read(2) != bytes((0x53, 0xEF)):
            raise ValueError("root partition is missing the ext filesystem magic")

        return {"size": size, "partitions": partitions}


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
    check = commands.add_parser("validate", help="validate the boot and root partition layout")
    check.add_argument("image")
    create = commands.add_parser("format-image", help="create a bootable CykOS image file")
    create.add_argument("image")
    create.add_argument("--kernel", default=os.path.join(ROOT, "bin", "KERNEL.BIN"))
    create.add_argument("--force", action="store_true", help="replace an existing image file")
    args = parser.parse_args()
    try:
        if args.command == "inspect":
            inspect(args.image)
        elif args.command == "validate":
            validate_image(args.image)
            print(f"Valid CykOS disk image: {args.image}")
        else:
            format_image(args.image, args.kernel, args.force)
    except (OSError, ValueError, RuntimeError) as exc:
        parser.error(str(exc))


if __name__ == "__main__":
    main()
