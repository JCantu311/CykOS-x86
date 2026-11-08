ENTRY(_start)
OUTPUT_FORMAT("binary")

SECTIONS
{
    /* Sector 1: Explicit MBR Boundary */
    . = 0x7C00;

    .boot_sector : {
        bin/boot.o(.boot)
    }

    /* Force the magic signature exactly at byte index 510 */
    . = 0x7C00 + 510;
    .mbr_sig : {
        SHORT(0xAA55)
    }

    /* Sector 2+: The Kernel Arena */
    /* VMA Execution mapping Base = 0x9000, LMA Physical File Offset = 512 */
    . = 0x9000;

    .text : AT(0x7C00 + 512) {
        bin/boot.o(.text*)       /* 32-bit entry code must map FIRST */
        *(.text*)                /* All executive code text routines */
    }

    /* FIX: Move read-only strings here so they accurately align with 0x9000 offsets */
    .rodata : {
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    .bss : {
        *(.bss*)
        *(COMMON)
    }

    /DISCARD/ : {
        *(.eh_frame)
        *(.note*)
    }
}
