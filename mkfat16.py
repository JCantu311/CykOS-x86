#!/usr/bin/env python3
"""
mkfat16.py - Generates / formats a FAT16 partition on an OS disk image.
Can also inject files (e.g. README.TXT, drivers, binaries) into the FAT16 filesystem.
"""

import sys
import os
import struct

def format_disk(image_path, files_to_add=None, directories_to_add=None):
    if files_to_add is None:
        files_to_add = {}
    if directories_to_add is None:
        directories_to_add = []

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
    cluster_size = SECTORS_PER_CLUSTER * BYTES_PER_SECTOR
    data_sectors = PARTITION_SECTORS - RESERVED_SECTORS - (NUM_FATS * SECTORS_PER_FAT) - ROOT_SECTORS
    data_clusters = data_sectors // SECTORS_PER_CLUSTER
    fat_clusters = (SECTORS_PER_FAT * BYTES_PER_SECTOR // 2) - 2
    available_clusters = min(data_clusters, fat_clusters)

    directories = {"/"}
    directory_entry_counts = {"/": 0}
    normalized_files = {}

    for directory_path in directories_to_add:
        parts = [part.upper() for part in directory_path.replace("\\", "/").split("/") if part]
        parent = "/"
        for component in parts:
            directory = (parent.rstrip("/") + "/" + component) if parent != "/" else "/" + component
            if directory not in directories:
                directories.add(directory)
                directory_entry_counts[directory] = 2
                directory_entry_counts[parent] += 1
            parent = directory

    for file_path, file_data in files_to_add.items():
        parts = [part.upper() for part in file_path.replace("\\", "/").split("/") if part]
        if not parts:
            raise ValueError(f"Invalid empty FAT16 file path: {file_path!r}")
        parent = "/"
        for component in parts[:-1]:
            directory = (parent.rstrip("/") + "/" + component) if parent != "/" else "/" + component
            if directory not in directories:
                directories.add(directory)
                directory_entry_counts[directory] = 2
                directory_entry_counts[parent] += 1
            parent = directory
        directory_entry_counts[parent] += 1
        normalized_files["/".join(parts)] = file_data

    if directory_entry_counts["/"] > ROOT_ENTRIES:
        raise ValueError(f"FAT16 root directory supports {ROOT_ENTRIES} entries, got {directory_entry_counts['/']}")

    required_clusters = 0
    for file_data in normalized_files.values():
        file_bytes = file_data.encode("utf-8") if isinstance(file_data, str) else file_data
        required_clusters += (len(file_bytes) + cluster_size - 1) // cluster_size
    required_clusters += sum((count + cluster_size // 32 - 1) // (cluster_size // 32)
                             for directory, count in directory_entry_counts.items() if directory != "/")
    if required_clusters > available_clusters:
        raise ValueError(
            f"FAT16 data area holds {available_clusters} clusters, but files need {required_clusters}"
        )

    # Open image (or create it) and discard stale overflow from previous builds.
    if not os.path.exists(image_path):
        with open(image_path, "wb") as f:
            f.truncate(TOTAL_SIZE_MB * 1024 * 1024)

    with open(image_path, "r+b") as f:
        f.truncate(TOTAL_SIZE_MB * 1024 * 1024)
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

        # 4. Initialize root and subdirectory entry storage.
        root_dir_size_bytes = ROOT_SECTORS * BYTES_PER_SECTOR
        root_dir = bytearray(root_dir_size_bytes)
        directory_state = {"/": {"clusters": [], "entry_count": 0}}
        fat_entries = [0] * (fat_size_bytes // 2)
        fat_entries[0] = 0xFFF8
        fat_entries[1] = 0xFFFF
        cluster_data = {}
        next_cluster = 2

        def allocate_cluster():
            nonlocal next_cluster
            if next_cluster >= len(fat_entries):
                raise ValueError("FAT16 allocation exceeded FAT entry capacity")
            cluster = next_cluster
            next_cluster += 1
            fat_entries[cluster] = 0xFFFF
            cluster_data[cluster] = bytearray(cluster_size)
            return cluster

        def make_entry(name, attributes, first_cluster, file_size):
            if name == ".":
                base, ext = b".       ", b"   "
            elif name == "..":
                base, ext = b"..      ", b"   "
            else:
                parts = name.split(".", 1)
                base = parts[0].ljust(8)[:8].encode("ascii")
                ext = parts[1].ljust(3)[:3].encode("ascii") if len(parts) > 1 else b"   "
            entry = bytearray(32)
            entry[0:8] = base
            entry[8:11] = ext
            entry[11] = attributes
            struct.pack_into("<H", entry, 26, first_cluster)
            struct.pack_into("<I", entry, 28, file_size)
            return entry

        def append_directory_entry(directory_path, entry):
            state = directory_state[directory_path]
            entries_per_cluster = cluster_size // 32
            cluster_index = state["entry_count"] // entries_per_cluster
            if directory_path == "/":
                slot = state["entry_count"]
                root_dir[slot * 32:(slot + 1) * 32] = entry
            else:
                if cluster_index >= len(state["clusters"]):
                    cluster = allocate_cluster()
                    if state["clusters"]:
                        fat_entries[state["clusters"][-1]] = cluster
                    state["clusters"].append(cluster)
                cluster = state["clusters"][cluster_index]
                slot = state["entry_count"] % entries_per_cluster
                cluster_data[cluster][slot * 32:(slot + 1) * 32] = entry
            state["entry_count"] += 1

        for directory in sorted(directories - {"/"}, key=lambda value: value.count("/")):
            parent, name = directory.rsplit("/", 1)
            parent = parent or "/"
            directory_state[directory] = {"clusters": [], "entry_count": 0, "parent": parent}
            first_cluster = allocate_cluster()
            directory_state[directory]["clusters"].append(first_cluster)
            append_directory_entry(directory, make_entry(".", 0x10, first_cluster, 0))
            parent_cluster = directory_state[parent]["clusters"][0] if parent != "/" else 0
            append_directory_entry(directory, make_entry("..", 0x10, parent_cluster, 0))
            append_directory_entry(parent, make_entry(name, 0x10, first_cluster, 0))

        # 5. Calculate data area start
        data_start_lba = PARTITION_START_LBA + RESERVED_SECTORS + (NUM_FATS * SECTORS_PER_FAT) + ROOT_SECTORS
        data_offset = data_start_lba * BYTES_PER_SECTOR

        # 6. Allocate and inject files.
        for file_path, file_data in normalized_files.items():
            if isinstance(file_data, str):
                file_bytes = file_data.encode("utf-8")
            else:
                file_bytes = file_data

            parent, _, file_name = file_path.rpartition("/")
            parent = "/" + parent if parent else "/"
            file_size = len(file_bytes)
            first_cluster = 0
            previous_cluster = 0
            for offset in range(0, file_size, cluster_size):
                cluster = allocate_cluster()
                if first_cluster == 0:
                    first_cluster = cluster
                if previous_cluster:
                    fat_entries[previous_cluster] = cluster
                previous_cluster = cluster
                chunk = file_bytes[offset:offset + cluster_size]
                cluster_data[cluster][:len(chunk)] = chunk

            append_directory_entry(parent, make_entry(file_name, 0x20, first_cluster, file_size))

        for cluster, data in cluster_data.items():
            f.seek(data_offset + (cluster - 2) * cluster_size)
            f.write(data)

        for index, value in enumerate(fat_entries):
            struct.pack_into("<H", fat, index * 2, value)

        for state in directory_state.values():
            clusters = state["clusters"]
            if clusters:
                for previous, following in zip(clusters, clusters[1:]):
                    fat_entries[previous] = following
                fat_entries[clusters[-1]] = 0xFFFF
        for index, value in enumerate(fat_entries):
            struct.pack_into("<H", fat, index * 2, value)

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
        "USR/SHARE/DOC/README.TXT": "Welcome to CykOS!\nFAT16 Filesystem initialized successfully.\n",
        "BOOT/MODULES.CFG": "# CykOS Boot-Time Kernel Module List\n/BOOT/DRIVERS/CYKEYBRD.DRV\n/BOOT/DRIVERS/CYKDISK.DRV\n/BOOT/DRIVERS/CYSOUND.DRV\n",
    }

    # Load USERCMDS.LST if present on host, otherwise use default
    if os.path.exists("USERCMDS.LST"):
        with open("USERCMDS.LST", "r") as f:
            sample_files["ETC/USERCMDS.LST"] = f.read()
    else:
        sample_files["ETC/USERCMDS.LST"] = "# CykOS External User Commands\nedit /USR/BIN/EDIT.BIN\nvi /USR/BIN/EDIT.BIN\n"

    # Automatically scan bin/ and apps/bin/ for all user binaries
    search_dirs = ["bin", os.path.join("apps", "bin")]
    for sdir in search_dirs:
        if os.path.exists(sdir):
            for fname in os.listdir(sdir):
                fpath = os.path.join(sdir, fname)
                if os.path.isfile(fpath):
                    target_name = fname.upper()
                    # Skip disk image and build artifacts
                    if target_name in ["OS_IMAGE.BIN", "CYKOS_BIN.BIN", "KERNEL.MAP", "KERNEL_SYMS.LD", "KERNEL.ELF"]:
                        continue
                    ext = os.path.splitext(fname)[1].upper()
                    if ext in [".BIN", ".COM", ".DRV"]:
                        with open(fpath, "rb") as bf:
                            content = bf.read()
                            if len(content) == 0:
                                print(f"[mkfat16] Warning: Skipping 0-byte file: {fname}")
                                continue
                            if len(content) > 196608:
                                print(f"[mkfat16] Warning: Skipping oversized application or disk image: {fname} ({len(content)} bytes)")
                                continue
                            if ext == ".DRV":
                                destination = f"BOOT/DRIVERS/{target_name}"
                            elif target_name == "SHELL.BIN":
                                destination = f"BIN/{target_name}"
                            else:
                                destination = f"USR/BIN/{target_name}"
                            sample_files[destination] = content
                            print(f"[mkfat16] Bundled Level 4 application: {target_name} ({len(content)} bytes)")

    directories = ["BIN", "BOOT/DRIVERS", "ETC", "HOME/USER", "MNT", "TMP", "USR/BIN", "USR/SHARE/DOC", "VAR/LOG"]
    format_disk(target_img, sample_files, directories)
