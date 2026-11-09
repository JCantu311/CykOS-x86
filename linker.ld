ENTRY(_start)
OUTPUT_FORMAT("binary")

SECTIONS
{
    /* Sector 1: Explicit MBR Boundary */
    . = 0x7C00;

    .boot_sector : {
        KEEP(*boot.o(.boot))
    }

    /* Force the magic signature exactly at byte index 510 */
    . = 0x7C00 + 510;
    .mbr_sig : {
        SHORT(0xAA55)
    }

    /* Sector 2+: The Kernel Arena (Contiguous with MBR at 0x7E00, VMA == LMA) */
    . = 0x7E00;
    _kernel_start = .;

    .text : {
        KEEP(*boot.o(.text*))    /* 32-bit assembly entry landing code goes FIRST */
        *(.text*)                /* All executive code routines */
        . = ALIGN(4);
        *(.rodata*)              /* Global string literals and constants */
        *(.rdata*)               /* Read-only data for MinGW/PE-COFF toolchains */
        . = ALIGN(4);
        *(.data*)                /* Initialized global variables */
        *(.data.rel.ro*)         /* Initialized pointers to read-only strings */
        *(.data.rel.ro.local*)
        *(.got*)
        *(.got.plt*)
    }

    . = ALIGN(4);
    _kernel_end = .;
    _kernel_size = _kernel_end - _kernel_start;
    _kernel_sectors = (_kernel_size + 511) / 512;

    .bss (NOLOAD) : {
        . = ALIGN(4);
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    }

    /DISCARD/ : {
        *(.eh_frame*)
        *(.note*)
        *(.comment)
        *(.debug*)
    }
}
