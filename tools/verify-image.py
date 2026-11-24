#!/usr/bin/env python3
"""Validate the CykOS MBR layout and run e2fsck read-only on its ext2 root."""

import argparse
import importlib.util
import os
import shutil
import subprocess
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SPEC = importlib.util.spec_from_file_location(
    "cykos_disk", os.path.join(ROOT, "tools", "cykos-disk.py"))
DISK_TOOL = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DISK_TOOL)


def verify(path):
    layout = DISK_TOOL.validate_image(path)
    ext_partitions = [part for _, part in layout["partitions"] if part["type"] == 0x83]
    if len(ext_partitions) != 1:
        raise ValueError("image does not have exactly one ext2 root partition")
    e2fsck = shutil.which("e2fsck")
    if not e2fsck:
        raise RuntimeError("e2fsck is required for filesystem verification")

    part = ext_partitions[0]
    offset = part["start"] * 512
    remaining = part["sectors"] * 512
    with tempfile.TemporaryDirectory(prefix="cykos-verify-") as temp_dir:
        root_path = os.path.join(temp_dir, "root.ext2")
        with open(path, "rb") as image, open(root_path, "wb") as root:
            image.seek(offset)
            while remaining:
                chunk = image.read(min(1024 * 1024, remaining))
                if not chunk:
                    raise ValueError("image ended while extracting the ext2 partition")
                root.write(chunk)
                remaining -= len(chunk)
        result = subprocess.run([e2fsck, "-fn", root_path], text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        print(result.stdout, end="")
        if result.returncode != 0:
            raise RuntimeError(f"e2fsck reported filesystem errors (status {result.returncode})")
    print(f"Verified CykOS image and clean ext2 root: {path}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image")
    args = parser.parse_args()
    try:
        verify(args.image)
    except (OSError, ValueError, RuntimeError) as exc:
        parser.error(str(exc))


if __name__ == "__main__":
    main()
