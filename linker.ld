ENTRY(_start)
OUTPUT_FORMAT("binary")

SECTIONS
{
    /* Sector 1: Explicit MBR Boundary */
    . = 0x7C00;

    .boot_sector : {
        *(.boot)
    }

    /* Force the magic signature exactly at byte index 510 */
    . = 0x7C00 + 510;
    .mbr_sig : {
        SHORT(0xAA55)
    }

    /* Sector 2+: The Kernel Arena (Execution map at 0x9000, file offset 512) */
    . = 0x9000;
    .text : AT(0x7C00 + 512) {
        *(.text*)
        *(.rodata*)
        *(.data*)
    }

    .bss : {
        *(.bss*)
        *(COMMON)
    }
}
