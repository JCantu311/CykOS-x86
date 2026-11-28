/* CykOS High-Address ELF32-i386 Linker Script
 * Places large static executables (GCC, cc1, as, ld, etc.) at 64 MiB (0x04000000),
 * giving up to ~700 MiB of virtual code+data+heap space, 224 MiB mmap arena,
 * and a 1 MiB stack at 0x3F000000.
 */
OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(_start)

SECTIONS
{
    . = 0x04000000;

    .text ALIGN(4096) : {
        *(.entry)
        *(.text .text.*)
    }

    .rodata ALIGN(4096) : {
        *(.rodata .rodata.*)
    }

    .data ALIGN(4096) : {
        *(.data .data.*)
    }

    .bss ALIGN(4096) : {
        *(COMMON)
        *(.bss .bss.*)
    }

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
