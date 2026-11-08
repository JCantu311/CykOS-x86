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
    . = 0x9000;

    .text : AT(0x7C00 + 512) {
        bin/boot.o(.text*)       /* 32-bit assembly entry landing code goes FIRST */
        *(.text*)                /* All executive code routines */
        *(.rodata*)              /* Merge read-only data straight into text */
        *(.data*)                /* Merge initialized global data variables */
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
