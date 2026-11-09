ENTRY(_start)
OUTPUT_FORMAT("binary")

SECTIONS
{
    /* Sector 1: Explicit MBR Boundary */
    . = 0x7C00;

    .boot_sector : {
        *boot.o(.boot)
    }

    /* Force the magic signature exactly at byte index 510 */
    . = 0x7C00 + 510;
    .mbr_sig : {
        SHORT(0xAA55)
    }

    /* Sector 2+: The Kernel Arena */
    . = 0x9000;

    /* All kernel references inherit 0x9000 VMA runtime base, mapped at LMA byte 512 */
    .text : AT(0x7C00 + 512) {
        *boot.o(.text*)          /* 32-bit assembly entry landing code goes FIRST */
        *(.text*)                /* All executive code routines */
        . = ALIGN(4);
        *(.rodata*)              /* Global string literals and constants */
        *(.rdata*)               /* Read-only data for MinGW/PE-COFF toolchains */
        . = ALIGN(4);
        *(.data*)                /* Initialized global variables */
        *(.data.rel.ro*)         /* Initialized pointers to read-only strings (e.g. char* p = "...") */
        *(.data.rel.ro.local*)
    }

    .bss (NOLOAD) : {
        . = ALIGN(4);
        *(.bss*)
        *(COMMON)
    }

    /DISCARD/ : {
        *(.eh_frame)
        *(.note*)
        *(.comment)
    }
}
