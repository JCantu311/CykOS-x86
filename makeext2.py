#!/usr/bin/env python3
"""
makeext2.py - Builds a FAT16 BIOS boot partition and ext2 CykOS root partition.
KERNEL.BIN is installed on FAT16; the complete filesystem tree is installed on ext2.
"""

import sys
import os
import struct
import tempfile
import subprocess
import shutil

def strip_elf_unloaded_tail(content):
    """Drop trailing ELF section/debug metadata while keeping all PT_LOAD bytes.

    The kernel loads applications from program headers and does not consume ELF
    section tables. This keeps large debug tables from making a small app exceed
    the on-disk/application-file limit.
    """
    if len(content) < 52 or content[:7] != b"\x7fELF\x01\x01\x01":
        return content

    try:
        header = struct.unpack_from("<16sHHIIIIIHHHHHH", content, 0)
        elf_type, machine = header[1], header[2]
        phoff, phentsize, phnum = header[5], header[9], header[10]
        if elf_type not in (2, 3) or machine != 3 or phentsize != 32 or not phnum or phnum > 32:
            return content
        if phoff > len(content) or phnum > (len(content) - phoff) // phentsize:
            return content

        load_end = 0
        for index in range(phnum):
            p_type, p_offset, _vaddr, _paddr, p_filesz, _memsz, _flags, _align = \
                struct.unpack_from("<IIIIIIII", content, phoff + index * phentsize)
            if p_type == 1:
                if p_offset > len(content) or p_filesz > len(content) - p_offset:
                    return content
                load_end = max(load_end, p_offset + p_filesz)

        if not load_end or load_end >= len(content):
            return content
        if phoff + phnum * phentsize > load_end:
            return content

        stripped = bytearray(content[:load_end])
        # ELF32 e_shoff, e_shentsize, e_shnum, and e_shstrndx.
        struct.pack_into("<I", stripped, 32, 0)
        struct.pack_into("<HHH", stripped, 46, 0, 0, 0)
        return bytes(stripped)
    except (struct.error, OverflowError):
        return content

def format_disk(image_path, files_to_add=None, directories_to_add=None):
    if files_to_add is None:
        files_to_add = {}
    if directories_to_add is None:
        directories_to_add = []

    default_mb = 256 if os.path.isdir("toolchain/root") else 80
    TOTAL_SIZE_MB = int(os.environ.get("CYKOS_IMAGE_SIZE_MB", str(default_mb)))
    TOTAL_SECTORS = TOTAL_SIZE_MB * 1024 * 1024 // 512

    PARTITION_START_LBA = 2048  # 1 MB offset
    PARTITION_SECTORS = 32768   # 16 MiB FAT16 boot volume; stage2 uses fixed FAT LBAs
    EXT2_START_LBA = PARTITION_START_LBA + PARTITION_SECTORS
    EXT2_SECTORS = TOTAL_SECTORS - EXT2_START_LBA

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

    # Ensure all parent directories exist first
    def ensure_parent_directories(directory_path):
        parts = [part.upper() for part in directory_path.replace("\\\\", "/").split("/") if part]
        parent = "/"
        for component in parts:
            directory = (parent.rstrip("/") + "/" + component) if parent != "/" else "/" + component
            if directory not in directories:
                directories.add(directory)
                directory_entry_counts[directory] = 0
            parent = directory

    # Ensure all parent directories exist for each directory in directories_to_add
    for directory_path in directories_to_add:
        ensure_parent_directories(directory_path)

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

    # Keep the BIOS payload and UEFI fallback loader on FAT16. The complete
    # runtime tree is written to ext2 below and mounted as the root filesystem.
    root_files = normalized_files
    root_directories = directories
    if "KERNEL.BIN" not in root_files:
        raise ValueError("FAT16 boot volume requires KERNEL.BIN")
    boot_paths = {"KERNEL.BIN", "KERNEL.ELF", "EFI/BOOT/BOOTX64.EFI"}
    normalized_files = {path: data for path, data in root_files.items() if path in boot_paths}
    directories = {"/"}
    directory_entry_counts = {"/": 0}
    for file_path in normalized_files:
        parts = file_path.split("/")
        parent = "/"
        for component in parts[:-1]:
            directory = (parent.rstrip("/") + "/" + component) if parent != "/" else "/" + component
            if directory not in directories:
                directories.add(directory)
                directory_entry_counts[directory] = 2  # '.' and '..'
                directory_entry_counts[parent] += 1
            parent = directory
        directory_entry_counts[parent] += 1

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
            0xEF,                      # Partition Type: EFI System / FAT16 BIOS volume
            b"\xff\xff\xff",           # CHS end
            PARTITION_START_LBA,       # LBA Start
            PARTITION_SECTORS          # Sector Count
        )
        f.write(mbr_entry)

        # The kernel still boots from FAT16, then mounts this ext2 partition as /
        ext2_entry = struct.pack(
            "<B3sB3sII", 0x00, b"\xff\xff\xff", 0x83, b"\xff\xff\xff",
            EXT2_START_LBA, EXT2_SECTORS
        )
        f.write(ext2_entry)

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

    # Build the ext2 root partition from the exact same logical tree. mke2fs and
    # debugfs operate on a temporary filesystem image, which is then embedded at
    # the Linux partition's LBA so host-side tools never mount the disk image.
    if not shutil.which("mke2fs") or not shutil.which("debugfs"):
        raise RuntimeError("ext2 image creation requires mke2fs and debugfs (e2fsprogs)")
    with tempfile.TemporaryDirectory(prefix="cykos-ext2-") as temp_dir:
        ext2_path = os.path.join(temp_dir, "root.ext2")
        with open(ext2_path, "wb") as ext2_file:
            ext2_file.truncate(EXT2_SECTORS * BYTES_PER_SECTOR)
        subprocess.run([
            "mke2fs", "-q", "-F", "-t", "ext2", "-b", "1024", "-I", "256",
            "-O", "filetype,sparse_super,large_file,^dir_index", ext2_path
        ], check=True)
        command_file = os.path.join(temp_dir, "debugfs.cmd")
        commands = []
        for directory in sorted(root_directories, key=lambda item: (item.count("/"), item)):
            if directory != "/":
                commands.append(f"mkdir {directory}")
        ext2_files = dict(root_files)
        for file_path, file_data in root_files.items():
            parent, _, leaf = file_path.rpartition("/")
            stem, suffix = os.path.splitext(leaf)
            if suffix == ".DRV" and len(stem) > 8:
                short_leaf = stem[:8] + suffix
                ext2_files[(parent + "/" if parent else "") + short_leaf] = file_data
        for index, (file_path, file_data) in enumerate(ext2_files.items()):
            host_path = os.path.join(temp_dir, f"file-{index}")
            with open(host_path, "wb") as staged:
                staged.write(file_data.encode("utf-8") if isinstance(file_data, str) else file_data)
            commands.append(f"write {host_path} /{file_path}")
        with open(command_file, "w", encoding="utf-8") as command_stream:
            command_stream.write("\n".join(commands) + "\n")
        result = subprocess.run(
            ["debugfs", "-w", "-f", command_file, ext2_path],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True
        )
        if result.returncode != 0 or "Could not" in result.stdout or "File not found" in result.stdout:
            raise RuntimeError(f"debugfs failed while populating ext2 root:\n{result.stdout}")
        with open(ext2_path, "rb") as ext2_file, open(image_path, "r+b") as disk:
            disk.seek(EXT2_START_LBA * BYTES_PER_SECTOR)
            while True:
                chunk = ext2_file.read(1024 * 1024)
                if not chunk:
                    break
                disk.write(chunk)

    print(f"[makeext2] FAT16 boot volume: LBA {PARTITION_START_LBA}, {PARTITION_SECTORS} sectors.")
    print(f"[makeext2] ext2 root volume: LBA {EXT2_START_LBA}, {EXT2_SECTORS} sectors.")
    print(f"[makeext2] FAT16 contains BIOS and UEFI boot files; ext2 contains {len(root_files)} root file(s). Total image size: {TOTAL_SIZE_MB}MB.")

if __name__ == "__main__":
    target_img = sys.argv[1] if len(sys.argv) > 1 else "bin/images/os_image.bin"
    
    with open(os.path.join("bin", "KERNEL.BIN"), "rb") as kernel_file:
        kernel_bytes = kernel_file.read()
    sample_files = {
        "KERNEL.BIN": kernel_bytes,
        "USR/SHARE/DOC/README.TXT": "Welcome to CykOS!\next2 root filesystem initialized successfully.\n",
        "BOOT/MODULES.CFG": "# CykOS Boot-Time Kernel Module List\n/BOOT/DRIVERS/CYKEYBRD.DRV\n/BOOT/DRIVERS/CYKDISK.DRV\n/BOOT/DRIVERS/CYSOUND.DRV\n/BOOT/DRIVERS/CYHDA.DRV\n/BOOT/DRIVERS/CYDISPLA.DRV\n",
        # Create the log entry at format time; boot then overwrites this file.
        "VAR/LOG/LOG.TXT": "",
    }

    uefi_kernel_path = os.path.join("bin", "KERNEL.ELF")
    uefi_loader_path = os.path.join("bin", "BOOTX64.EFI")
    if os.path.isfile(uefi_kernel_path) and os.path.isfile(uefi_loader_path):
        with open(uefi_kernel_path, "rb") as uefi_kernel:
            sample_files["KERNEL.ELF"] = uefi_kernel.read()
        with open(uefi_loader_path, "rb") as uefi_loader:
            sample_files["EFI/BOOT/BOOTX64.EFI"] = uefi_loader.read()

    lua_smoke_path = os.path.join("ports", "lua", "tests", "smoke.lua")
    if os.path.isfile(lua_smoke_path):
        with open(lua_smoke_path, "r", encoding="utf-8") as lua_test:
            sample_files["USR/BIN/TESTS/LUASMOKE.LUA"] = lua_test.read()

    # Prefer the manifest-derived module list generated by make.sh.
    module_cfg_path = os.path.join("bin", "MODULES.CFG")
    if os.path.isfile(module_cfg_path):
        with open(module_cfg_path, "r", encoding="utf-8") as cfg_file:
            sample_files["BOOT/MODULES.CFG"] = cfg_file.read()

    # Load USERCMDS.LST if present on host, otherwise use default
    if os.path.exists("USERCMDS.LST"):
        with open("USERCMDS.LST", "r") as f:
            sample_files["ETC/USERCMDS.LST"] = f.read()
    else:
        sample_files["ETC/USERCMDS.LST"] = "# CykOS External User Commands\nedit /USR/BIN/EDIT.BIN\nvi /USR/BIN/EDIT.BIN\n"

    # Optional CykShell startup command list, installed in the standard config dir.
    autoexec_path = "AUTOEXEC.SHE"
    if os.path.exists(autoexec_path):
        with open(autoexec_path, "r", encoding="utf-8") as autoexec_file:
            sample_files["ETC/AUTOEXEC.SHE"] = autoexec_file.read()
    else:
        sample_files["ETC/AUTOEXEC.SHE"] = (
            "# CykOS startup commands for CykShell.\n"
            "# One command per line; lines beginning with # are ignored.\n"
        )

    # Keep diagnostics and demonstration programs separate from normal apps.
    test_binaries = {
        "HELLO.BIN", "SYSDEMO.BIN", "POSIXTEST.BIN", "SOUNDTEST.BIN", "GFXDEMO.BIN",
        "IDPROBE.BIN"
    }

    # Automatically scan bin/ and apps/bin/ for all user binaries
    search_dirs = ["bin", os.path.join("bin", "DRIVERS"), os.path.join("apps", "bin")]
    for sdir in search_dirs:
        if os.path.exists(sdir):
            for fname in os.listdir(sdir):
                fpath = os.path.join(sdir, fname)
                if os.path.isfile(fpath):
                    target_name = fname.upper()
                    # Skip disk image and build artifacts
                    if target_name in ["OS_IMAGE.BIN", "CYKOS_BIN.BIN", "KERNEL.BIN", "KERNEL.MAP", "KERNEL_SYMS.LD", "KERNEL.ELF", "STAGE2.BIN", "MBR.BIN"]:
                        continue
                    ext = os.path.splitext(fname)[1].upper()
                    if ext in [".BIN", ".COM", ".DRV", ".WAD", ".SO"]:
                        if ext == ".WAD":
                            destination = f"USR/SHARE/GAMES/DOOM/{target_name}"
                        elif ext == ".DRV":
                            destination = f"BOOT/DRIVERS/{target_name}"
                        elif ext == ".SO":
                            destination = f"USR/LIB/{target_name}"
                        elif target_name == "SHELL.BIN":
                            destination = f"BIN/{target_name}"
                        elif target_name in test_binaries:
                            destination = f"USR/BIN/TESTS/{target_name}"
                        else:
                            destination = f"USR/BIN/{target_name}"

                        # bin/ contains artifacts from this clean build. Keep
                        # them when the legacy apps/bin directory also has an
                        # older copy with the same installed path.
                        if destination in sample_files:
                            print(f"[makeext2] Keeping earlier build artifact for {destination}; skipping {fpath}")
                            continue

                        with open(fpath, "rb") as bf:
                            content = bf.read()
                            if ext in [".BIN", ".COM"] and len(content) > 524288:
                                original_size = len(content)
                                content = strip_elf_unloaded_tail(content)
                                if len(content) < original_size:
                                    print(f"[makeext2] Removed non-loadable ELF tail from {fname}: "
                                          f"{original_size} -> {len(content)} bytes")
                            if len(content) == 0:
                                print(f"[makeext2] Warning: Skipping 0-byte file: {fname}")
                                continue
                            if ext in [".BIN", ".COM"] and len(content) > 33554432:
                                print(f"[makeext2] Warning: Skipping application larger than 32 MiB: {fname} ({len(content)} bytes)")
                                continue
                            sample_files[destination] = content
                            if ext == ".WAD":
                                print(f"[makeext2] Bundled Doom game data: {target_name} ({len(content)} bytes)")
                            else:
                                print(f"[makeext2] Bundled Level 4 application: {target_name} ({len(content)} bytes)")

    directories = ["BIN", "BOOT/DRIVERS", "ETC", "HOME/USER", "MNT", "TMP", "USR/BIN", "USR/BIN/TESTS", "USR/LIB", "USR/SHARE/DOC", "USR/SHARE/GAMES/DOOM", "VAR/LOG", "usr", "usr/bin", "usr/include", "usr/lib", "usr/libexec", "tmp"]
    dir_set = set(directories)

    def ensure_parent_dirs(rel_path):
        parts = rel_path.split("/")[:-1]
        cur = ""
        for p in parts:
            cur = f"{cur}/{p}" if cur else p
            if cur not in dir_set:
                dir_set.add(cur)
                directories.append(cur)

    # Bundle CykOS C headers & runtime libraries into /usr/include and /usr/lib
    if os.path.isdir("libc/include"):
        for root_dir, _, files in os.walk("libc/include"):
            for fname in files:
                fpath = os.path.join(root_dir, fname)
                rel = os.path.relpath(fpath, "libc/include").replace("\\", "/")
                dest = f"usr/include/{rel}"
                ensure_parent_dirs(dest)
                with open(fpath, "rb") as hf:
                    sample_files[dest] = hf.read()

    for src_path, dest_path in [
        ("bin/libc.a", "usr/lib/libc.a"),
        ("bin/libc_objs/crt0.o", "usr/lib/crt0.o"),
        ("toolchain/cykos-high.ld", "usr/lib/cykos-high.ld"),
    ]:
        if os.path.isfile(src_path):
            ensure_parent_dirs(dest_path)
            with open(src_path, "rb") as lf:
                sample_files[dest_path] = lf.read()

    # Bundle cross-compiled native toolchain (Binutils + GCC) if present
    if os.path.isdir("toolchain/root"):
        tc_count = 0
        for root_dir, _, files in os.walk("toolchain/root"):
            for fname in files:
                fpath = os.path.join(root_dir, fname)
                rel = os.path.relpath(fpath, "toolchain/root").replace("\\", "/")
                ensure_parent_dirs(rel)
                with open(fpath, "rb") as tf:
                    sample_files[rel] = tf.read()
                tc_count += 1
        print(f"[makeext2] Bundled {tc_count} toolchain file(s) from toolchain/root.")

    format_disk(target_img, sample_files, directories)

