ENTRY(_start)
OUTPUT_FORMAT("binary")

SECTIONS
{
    /* Sector 1: Explicit MBR Boundary */
    . = 0x7C00;

    .boot_sector : {
        bin/boot.o(.text*)
    }

    /* Force the magic signature exactly at the 510-511 byte block */
    . = 0x7C00 + 510;
    .mbr_sig : {
        SHORT(0xAA55)
    }

    /* Sector 2+: The Kernel Arena */
    /* Tell the compiler execution code runs at 0x9000, but sits at file offset 512 */
    . = 0x9000;
    .text : AT(0x7C00 + 512) {
        bin/kernel.o(.text*)
        bin/cykastd.o(.text*)
        *(.text*)
        *(.rodata*)
        *(.data*)
    }

    .bss : {
        *(.bss*)
        *(COMMON)
    }
}
