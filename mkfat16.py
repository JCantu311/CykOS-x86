#!/usr/bin/env python3
"""
mkfat16.py - Generates / formats a FAT16 partition on an OS disk image.
Can also inject files (e.g. README.TXT, drivers, binaries) into the FAT16 filesystem.
"""

import sys
import os
import struct

def format_disk(image_path, files_to_add=None):
    if files_to_add is None:
        files_to_add = {}

    TOTAL_SIZE_MB = 16
    TOTAL_SECTORS = TOTAL_SIZE_MB * 1024 * 1024 // 512  # 32,768 sectors

    PARTITION_START_LBA = 2048  # 1 MB offset
    PARTITION_SECTORS = TOTAL_SECTORS - PARTITION_START_LBA # 30,720 sectors

    BYTES_PER_SECTOR = 512
    SECTORS_PER_CLUSTER = 4     # 2 KB clusters
    RESERVED_SECTORS = 4
    NUM_FATS = 2
    ROOT_ENTRIES = 512          # 512 * 32 = 16,384 bytes = 32 sectors
    ROOT_SECTORS = (ROOT_ENTRIES * 32) // BYTES_PER_SECTOR # 32 sectors
    SECTORS_PER_FAT = 32

    # Open image (or create if doesn't exist)
    if not os.path.exists(image_path):
        with open(image_path, "wb") as f:
            f.truncate(TOTAL_SIZE_MB * 1024 * 1024)
    else:
        # Enlarge to at least TOTAL_SIZE_MB
        current_size = os.path.getsize(image_path)
        if current_size < TOTAL_SIZE_MB * 1024 * 1024:
            with open(image_path, "r+b") as f:
                f.truncate(TOTAL_SIZE_MB * 1024 * 1024)

    with open(image_path, "r+b") as f:
        # 1. Update MBR Partition Table at byte 446 (0x1BE) of Sector 0
        f.seek(446)
        mbr_entry = struct.pack(
            "<B3sB3sII",
            0x80,                      # Bootable status
            b"\x00\x02\x00",           # CHS start
            0x06,                      # Partition Type: FAT16
            b"\xff\xff\xff",           # CHS end
            PARTITION_START_LBA,       # LBA Start
            PARTITION_SECTORS          # Sector Count
        )
        f.write(mbr_entry)

        # Ensure MBR signature at byte 510
        f.seek(510)
        f.write(b"\x55\xAA")

        # 2. Write FAT16 BPB at PARTITION_START_LBA
        part_offset = PARTITION_START_LBA * BYTES_PER_SECTOR
        f.seek(part_offset)

        bpb = bytearray(512)
        # Jump boot code + NOP
        bpb[0:3] = b"\xEB\x3C\x90"
        # OEM identifier
        bpb[3:11] = b"MSWIN4.1"
        # BPB fields
        struct.pack_into("<H", bpb, 11, BYTES_PER_SECTOR)
        struct.pack_into("<B", bpb, 13, SECTORS_PER_CLUSTER)
        struct.pack_into("<H", bpb, 14, RESERVED_SECTORS)
        struct.pack_into("<B", bpb, 16, NUM_FATS)
        struct.pack_into("<H", bpb, 17, ROOT_ENTRIES)
        struct.pack_into("<H", bpb, 19, PARTITION_SECTORS if PARTITION_SECTORS < 65536 else 0)
        struct.pack_into("<B", bpb, 21, 0xF8) # Media descriptor: Hard disk
        struct.pack_into("<H", bpb, 22, SECTORS_PER_FAT)
        struct.pack_into("<H", bpb, 24, 63)   # Sectors per track
        struct.pack_into("<H", bpb, 26, 16)   # Head count
        struct.pack_into("<I", bpb, 28, PARTITION_START_LBA) # Hidden sectors
        struct.pack_into("<I", bpb, 32, PARTITION_SECTORS if PARTITION_SECTORS >= 65536 else 0)

        # Extended BIOS Parameter Block (FAT16)
        struct.pack_into("<B", bpb, 36, 0x80)       # Drive number (0x80 = primary HDD)
        struct.pack_into("<B", bpb, 37, 0x00)       # Reserved
        struct.pack_into("<B", bpb, 38, 0x29)       # Extended boot signature
        struct.pack_into("<I", bpb, 39, 0x20261109) # Volume Serial ID
        bpb[43:54] = b"CYKOS_DISK "                 # Volume Label (11 bytes)
        bpb[54:62] = b"FAT16   "                     # Filesystem Type (8 bytes)
        # Boot signature at end of sector
        bpb[510:512] = b"\x55\xAA"

        f.write(bpb)

        # 3. Initialize FAT1 and FAT2
        fat_size_bytes = SECTORS_PER_FAT * BYTES_PER_SECTOR
        fat = bytearray(fat_size_bytes)
        # Cluster 0: Media descriptor (0xFFF8), Cluster 1: EOF marker (0xFFFF)
        struct.pack_into("<H", fat, 0, 0xFFF8)
        struct.pack_into("<H", fat, 2, 0xFFFF)

        # 4. Initialize Root Directory
        root_dir_size_bytes = ROOT_SECTORS * BYTES_PER_SECTOR
        root_dir = bytearray(root_dir_size_bytes)

        # 5. Calculate data area start
        data_start_lba = PARTITION_START_LBA + RESERVED_SECTORS + (NUM_FATS * SECTORS_PER_FAT) + ROOT_SECTORS
        data_offset = data_start_lba * BYTES_PER_SECTOR

        # 6. Inject files
        next_cluster = 2
        root_entry_index = 0

        for file_name, file_data in files_to_add.items():
            if isinstance(file_data, str):
                file_bytes = file_data.encode("utf-8")
            else:
                file_bytes = file_data

            # Split into 8.3 format
            name_parts = file_name.upper().split(".")
            base_name = name_parts[0].ljust(8)[:8].encode("ascii")
            ext = name_parts[1].ljust(3)[:3].encode("ascii") if len(name_parts) > 1 else b"   "

            start_cluster = next_cluster
            file_size = len(file_bytes)

            # Write directory entry (32 bytes)
            entry_offset = root_entry_index * 32
            root_dir[entry_offset:entry_offset+8] = base_name
            root_dir[entry_offset+8:entry_offset+11] = ext
            root_dir[entry_offset+11] = 0x20 # Archive attribute
            struct.pack_into("<H", root_dir, entry_offset+26, start_cluster)
            struct.pack_into("<I", root_dir, entry_offset+28, file_size)
            root_entry_index += 1

            # Allocate clusters and write data
            bytes_written = 0
            curr_cluster = start_cluster

            cluster_size = SECTORS_PER_CLUSTER * BYTES_PER_SECTOR
            while bytes_written < file_size:
                chunk = file_bytes[bytes_written:bytes_written + cluster_size]
                f.seek(data_offset + (curr_cluster - 2) * cluster_size)
                f.write(chunk)
                bytes_written += len(chunk)

                if bytes_written < file_size:
                    next_alloc = curr_cluster + 1
                    struct.pack_into("<H", fat, curr_cluster * 2, next_alloc)
                    curr_cluster = next_alloc
                else:
                    struct.pack_into("<H", fat, curr_cluster * 2, 0xFFFF) # EOF
                    next_cluster = curr_cluster + 1

        # Write FAT1
        fat1_offset = (PARTITION_START_LBA + RESERVED_SECTORS) * BYTES_PER_SECTOR
        f.seek(fat1_offset)
        f.write(fat)

        # Write FAT2
        fat2_offset = fat1_offset + fat_size_bytes
        f.seek(fat2_offset)
        f.write(fat)

        # Write Root Directory
        root_dir_offset = fat2_offset + fat_size_bytes
        f.seek(root_dir_offset)
        f.write(root_dir)

    print(f"[mkfat16] FAT16 volume formatted at LBA {PARTITION_START_LBA} ({PARTITION_SECTORS} sectors).")
    print(f"[mkfat16] Injected {len(files_to_add)} file(s). Total image size: {TOTAL_SIZE_MB}MB.")

if __name__ == "__main__":
    target_img = sys.argv[1] if len(sys.argv) > 1 else "bin/os_image.bin"
    
    sample_files = {
        "README.TXT": "Welcome to CykOS!\nFAT16 Filesystem initialized successfully.\n",
        "MODULES.CFG": "# CykOS Boot-Time Kernel Module List\nCYKEYBRD.DRV\n",
    }

    # Load USERCMDS.LST if present on host, otherwise use default
    if os.path.exists("USERCMDS.LST"):
        with open("USERCMDS.LST", "r") as f:
            sample_files["USERCMDS.LST"] = f.read()
    else:
        sample_files["USERCMDS.LST"] = "# CykOS External User Commands\nedit EDIT.BIN\nvi EDIT.BIN\n"

    # Automatically scan bin/ and apps/bin/ for all user binaries
    search_dirs = ["bin", os.path.join("apps", "bin")]
    for sdir in search_dirs:
        if os.path.exists(sdir):
            for fname in os.listdir(sdir):
                fpath = os.path.join(sdir, fname)
                if os.path.isfile(fpath):
                    target_name = fname.upper()
                    # Skip disk image and build artifacts
                    if target_name in ["OS_IMAGE.BIN", "KERNEL.MAP", "KERNEL_SYMS.LD", "KERNEL.ELF"]:
                        continue
                    ext = os.path.splitext(fname)[1].upper()
                    if ext in [".BIN", ".COM", ".DRV"]:
                        with open(fpath, "rb") as bf:
                            sample_files[target_name] = bf.read()
                            print(f"[mkfat16] Bundled Level 4 application: {target_name} ({len(sample_files[target_name])} bytes)")

    format_disk(target_img, sample_files)
