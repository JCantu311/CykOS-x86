#!/usr/bin/env python3
"""Build a dual BIOS/UEFI El Torito ISO containing the current CykOS image."""

import argparse
import os
import shutil
import struct
import subprocess
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SECTOR = 2048
BOOT_SIZE = 8192


def run(command):
    subprocess.run(command, cwd=ROOT, check=True)


def locate_root_file(iso_path, wanted):
    """Return the ISO9660 extent LBA for a file in the root directory."""
    with open(iso_path, "rb") as stream:
        stream.seek(16 * SECTOR)
        pvd = stream.read(SECTOR)
        if len(pvd) != SECTOR or pvd[0] != 1 or pvd[1:6] != b"CD001":
            raise RuntimeError("xorriso output has no ISO9660 primary volume descriptor")
        root_record = pvd[156:190]
        if len(root_record) < 34 or root_record[0] < 34:
            raise RuntimeError("ISO9660 root directory record is invalid")
        root_lba = struct.unpack_from("<I", root_record, 2)[0]
        root_size = struct.unpack_from("<I", root_record, 10)[0]
        stream.seek(root_lba * SECTOR)
        directory = stream.read(root_size)

    offset = 0
    while offset < len(directory):
        length = directory[offset]
        if length == 0:
            offset = ((offset // SECTOR) + 1) * SECTOR
            continue
        record = directory[offset:offset + length]
        if length < 34 or len(record) != length:
            raise RuntimeError("ISO9660 directory record is malformed")
        name_len = record[32]
        name = record[33:33 + name_len].decode("ascii", "replace")
        if name not in ("\x00", "\x01") and name.split(";", 1)[0].upper() == wanted.upper():
            return struct.unpack_from("<I", record, 2)[0]
        offset += length
    raise RuntimeError(f"{wanted} not found in ISO root directory")


def verify_embedded_file(iso_path, lba, source_path):
    """Verify the ISO contains the exact source bytes at its recorded extent."""
    with open(iso_path, "rb") as iso, open(source_path, "rb") as source:
        iso.seek(lba * SECTOR)
        while True:
            expected = source.read(1024 * 1024)
            if not expected:
                return
            actual = iso.read(len(expected))
            if actual != expected:
                raise RuntimeError(
                    f"embedded file does not match source: {os.path.basename(source_path)}"
                )


def assemble_installer(output, image_lba, image_blocks, kernel_lba, kernel_blocks):
    run([
        "nasm", "-f", "bin",
        f"-DOS_IMAGE_LBA={image_lba}",
        f"-DOS_IMAGE_BLOCKS={image_blocks}",
        f"-DKERNEL_ISO_LBA={kernel_lba}",
        f"-DKERNEL_BLOCKS={kernel_blocks}",
        "src/installer/bios_installer.asm", "-o", output,
    ])
    if os.path.getsize(output) != BOOT_SIZE:
        raise RuntimeError(f"installer boot image must be {BOOT_SIZE} bytes")


def create_iso(staging, output, uefi=False):
    command = [
        "xorriso", "-as", "mkisofs", "-quiet", "-o", output,
        "-V", "CYKOS_INSTALL", "-iso-level", "1",
        "-b", "boot/installer.bin", "-no-emul-boot", "-boot-load-size", "16",
    ]
    if uefi:
        command += ["-eltorito-alt-boot", "-e", "boot/efiboot.img",
                    "-no-emul-boot", "-efi-boot-part", "--efi-boot-image"]
    command.append(staging)
    run(command)


def create_uefi_boot(staging, temporary, kernel_elf):
    """Create an EFI El Torito image with GRUB and a UEFI live entry."""
    config_path = os.path.join(temporary, "grub-uefi.cfg")
    with open(config_path, "w", encoding="ascii") as config:
        config.write("""set timeout=5
set default=0
menuentry 'CykOS live (UEFI)' {
    search --no-floppy --file --set=root /CYKOS.IMG
    multiboot2 /KERNEL.ELF
    module2 /CYKOS.IMG cykos-image
    boot
}
""")

    loader = os.path.join(temporary, "BOOTX64.EFI")
    modules = "part_msdos iso9660 multiboot2 normal search search_fs_file terminal efi_gop video video_fb gfxterm font"
    run([
        "grub-mkstandalone", "--format=x86_64-efi", "--output", loader,
        "--compress=xz", "--locales=", "--fonts=", "--themes=",
        f"--install-modules={modules}", f"--modules={modules}",
        f"boot/grub/grub.cfg={config_path}",
    ])

    efi_image = os.path.join(staging, "boot", "efiboot.img")
    run(["mformat", "-i", efi_image, "-C", "-f", "2880", "::"])
    run(["mmd", "-i", efi_image, "::/EFI", "::/EFI/BOOT"])
    run(["mcopy", "-i", efi_image, loader, "::/EFI/BOOT/BOOTX64.EFI"])
    removable_path = os.path.join(staging, "EFI", "BOOT")
    os.makedirs(removable_path)
    shutil.copyfile(loader, os.path.join(removable_path, "BOOTX64.EFI"))
    shutil.copyfile(kernel_elf, os.path.join(staging, "KERNEL.ELF"))
    return efi_image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", default="bin/images/os_image.bin")
    parser.add_argument("--kernel", default="bin/KERNEL.BIN")
    parser.add_argument("--kernel-elf", default="bin/KERNEL.ELF")
    parser.add_argument("--output", default="bin/images/cykos-installer.iso")
    args = parser.parse_args()
    image_path = os.path.abspath(args.image)
    kernel_path = os.path.abspath(args.kernel)
    kernel_elf_path = os.path.abspath(args.kernel_elf)
    output_path = os.path.abspath(args.output)
    for tool in ("nasm", "xorriso"):
        if not shutil.which(tool):
            raise SystemExit(f"required ISO build tool not found: {tool}")
    if not os.path.isfile(image_path) or os.path.getsize(image_path) == 0:
        raise SystemExit(f"CykOS disk image not found or empty: {image_path}")
    if not os.path.isfile(kernel_path) or os.path.getsize(kernel_path) == 0:
        raise SystemExit(f"CykOS kernel not found or empty: {kernel_path}")
    if os.path.getsize(kernel_path) > 0x80000:
        raise SystemExit("CykOS kernel exceeds the BIOS live loader's 512 KiB limit")
    if os.path.getsize(image_path) % SECTOR:
        raise SystemExit("CykOS image size must be a multiple of 2048 bytes")
    if os.path.realpath(image_path) == os.path.realpath(output_path):
        raise SystemExit("installer ISO path must differ from disk image path")
    if os.path.realpath(kernel_path) == os.path.realpath(output_path):
        raise SystemExit("installer ISO path must differ from kernel path")

    output_directory = os.path.dirname(output_path)
    if output_directory:
        os.makedirs(output_directory, exist_ok=True)
    image_blocks = os.path.getsize(image_path) // SECTOR
    kernel_blocks = (os.path.getsize(kernel_path) + SECTOR - 1) // SECTOR
    with tempfile.TemporaryDirectory(prefix="cykos-installer-") as temporary:
        staging = os.path.join(temporary, "iso-root")
        os.makedirs(os.path.join(staging, "boot"))
        shutil.copyfile(image_path, os.path.join(staging, "CYKOS.IMG"))
        shutil.copyfile(kernel_path, os.path.join(staging, "KERNEL.BIN"))
        boot_path = os.path.join(staging, "boot", "installer.bin")
        assemble_installer(boot_path, 0, image_blocks, 0, kernel_blocks)

        uefi = all(shutil.which(tool) for tool in
                   ("grub-mkstandalone", "mformat", "mmd", "mcopy"))
        if uefi and os.path.isfile(kernel_elf_path) and os.path.getsize(kernel_elf_path):
            create_uefi_boot(staging, temporary, kernel_elf_path)
        else:
            print("WARNING: UEFI El Torito boot omitted (requires KERNEL.ELF, "
                  "grub-mkstandalone, and mtools).")

        # The first ISO pass determines the image's extent. The boot image has
        # fixed size, so its second pass retains the same extent.
        first_iso = os.path.join(temporary, "layout.iso")
        create_iso(staging, first_iso, uefi)
        image_lba = locate_root_file(first_iso, "CYKOS.IMG")
        kernel_lba = locate_root_file(first_iso, "KERNEL.BIN")
        assemble_installer(boot_path, image_lba, image_blocks, kernel_lba, kernel_blocks)
        create_iso(staging, output_path, uefi)
        final_lba = locate_root_file(output_path, "CYKOS.IMG")
        final_kernel_lba = locate_root_file(output_path, "KERNEL.BIN")
        if final_lba != image_lba or final_kernel_lba != kernel_lba:
            raise RuntimeError("ISO file extent changed between passes; refusing stale installer")
        verify_embedded_file(output_path, final_lba, image_path)
        verify_embedded_file(output_path, final_kernel_lba, kernel_path)

    print(f"BIOS/UEFI installer ISO created: {output_path}" if uefi else
          f"BIOS installer ISO created: {output_path}")
    print(f"Embedded CykOS image: ISO LBA {image_lba}, {image_blocks} blocks")
    print(f"Live kernel: ISO LBA {kernel_lba}, {kernel_blocks} blocks")


if __name__ == "__main__":
    main()
