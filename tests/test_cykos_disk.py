import os
import struct
import importlib.util
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SPEC = importlib.util.spec_from_file_location("cykos_disk", os.path.join(ROOT, "tools", "cykos-disk.py"))
DISK_TOOL = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DISK_TOOL)
validate_image = DISK_TOOL.validate_image


class DiskImageValidationTests(unittest.TestCase):
    def make_image(self, *, ext_magic=b"\x53\xef", overlap=False):
        sectors = 4096
        image = bytearray(sectors * 512)
        image[510:512] = b"\x55\xaa"
        fat_start, fat_count = 1, 128
        ext_start = 64 if overlap else 256
        ext_count = 512
        entries = (
            (0x80, 0xEF, fat_start, fat_count),
            (0x00, 0x83, ext_start, ext_count),
        )
        for index, (boot, kind, start, count) in enumerate(entries):
            struct.pack_into("<B3sB3sII", image, 446 + index * 16,
                             boot, b"\0\0\0", kind, b"\0\0\0", start, count)
        image[fat_start * 512 + 54:fat_start * 512 + 62] = b"FAT16   "
        image[ext_start * 512 + 1024 + 56:ext_start * 512 + 1024 + 58] = ext_magic
        handle = tempfile.NamedTemporaryFile(delete=False)
        handle.write(image)
        handle.close()
        self.addCleanup(lambda: os.unlink(handle.name))
        return handle.name

    def test_accepts_expected_boot_and_root_layout(self):
        layout = validate_image(self.make_image())
        self.assertEqual([part["type"] for _, part in layout["partitions"]], [0xEF, 0x83])

    def test_rejects_missing_mbr_signature(self):
        path = self.make_image()
        with open(path, "r+b") as image:
            image.seek(510)
            image.write(b"\0\0")
        with self.assertRaisesRegex(ValueError, "MBR signature"):
            validate_image(path)

    def test_rejects_out_of_bounds_partition(self):
        path = self.make_image()
        with open(path, "r+b") as image:
            image.seek(446 + 16 + 12)
            image.write(struct.pack("<I", 0xFFFFFFFF))
        with self.assertRaisesRegex(ValueError, "beyond the image"):
            validate_image(path)

    def test_rejects_overlapping_partitions(self):
        with self.assertRaisesRegex(ValueError, "overlap"):
            validate_image(self.make_image(overlap=True))

    def test_rejects_invalid_ext_superblock(self):
        with self.assertRaisesRegex(ValueError, "ext filesystem magic"):
            validate_image(self.make_image(ext_magic=b"\0\0"))


if __name__ == "__main__":
    unittest.main()
