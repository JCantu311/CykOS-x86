ENTRY(_start)
OUTPUT_FORMAT("binary")

SECTIONS
{
    /* Sector 1: The Master Boot Record (MBR) */
    . = 0x7C00;
    .boot_sector : {
        bin/boot.o(.boot)
    }

    /* Force the MBR signature exactly at byte 510 of the file */
    . = 0x7C00 + 510;
    .mbr_sig : {
        bin/boot.o(.mbr_signature)
    }

    /* Sector 2+: The Actual Kernel Code */
    /* This begins immediately after the first 512 bytes, mapped to address 0x9000 */
    . = 0x9000;
    .text : AT(0x7C00 + 512) {
        *(.text*)
    }

    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }
    .bss    : { *(.bss*) }
}
