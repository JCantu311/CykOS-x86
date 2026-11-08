ENTRY(_start)
OUTPUT_FORMAT("binary")

SECTIONS
{
    /* Sector 1: Explicit MBR Boundary */
    . = 0x7C00;

    .boot_sector : {
        bin/boot.o(.boot)
    }

    /* Force the magic signature exactly at the 510-511 byte block */
    . = 0x7C00 + 510;
    .mbr_sig : {
        SHORT(0xAA55)
    }

    /* Sector 2+: The Kernel Arena */
    /* VMA = 0x9000, LMA = 0x7C00 + 512 */
    . = 0x9000;
    .text : AT(0x7C00 + 512) {
        bin/boot.o(.text*)       /* Fix: Place the 32-bit entry assembly from boot.o FIRST */
        
        /* Bundle code, strings, and data sequentially for your C objects */
        bin/kernel.o(.text* .rodata* .data*)
        bin/cykastd.o(.text* .rodata* .data*)
        
        *(.text*)
        *(.rodata*)
        *(.data*)
    }

    .bss : {
        *(.bss*)
        *(COMMON)
    }
}
