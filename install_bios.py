#!/usr/bin/env python3
"""Install the BIOS MBR and its fixed-size FAT16 loader on the disk image."""
import os
import sys

IMAGE = sys.argv[1] if len(sys.argv) > 1 else "bin/images/os_image.bin"
MBR = "bin/mbr.bin"
STAGE2 = "bin/stage2.bin"
STAGE2_SECTORS = 16
SECTOR = 512

with open(MBR, "rb") as f:
    mbr = f.read()
with open(STAGE2, "rb") as f:
    stage2 = f.read()
if len(mbr) != SECTOR:
    raise SystemExit(f"BIOS MBR must be exactly 512 bytes, got {len(mbr)}")
if len(stage2) > STAGE2_SECTORS * SECTOR:
    raise SystemExit(f"BIOS stage 2 exceeds {STAGE2_SECTORS} sectors")
if not os.path.isfile(IMAGE):
    raise SystemExit(f"disk image not found: {IMAGE}")

with open(IMAGE, "r+b") as f:
    # Keep the partition table written by makeext2.py; install boot code only.
    f.seek(0)
    f.write(mbr[:446])
    f.seek(SECTOR)
    f.write(stage2.ljust(STAGE2_SECTORS * SECTOR, b"\0"))

print(f"[BIOS] Installed stage 1 and FAT16 stage 2 into {IMAGE}")
