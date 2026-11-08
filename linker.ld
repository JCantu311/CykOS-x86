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
    /* VMA Execution Base = 0x9000, LMA File Offset = 512 */
    . = 0x9000;

    .text : AT(0x7C00 + 512) {
        bin/boot.o(.text*)       /* 32-bit assembly entry landing code goes FIRST */
        
        /* Force each C object to layout its text and string constants together */
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
