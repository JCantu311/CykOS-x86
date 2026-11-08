ENTRY(_start)
OUTPUT_FORMAT("binary")

SECTIONS
{
    /* Sector 1: Explicit MBR boundary */
    . = 0x7C00;
    
    .boot_sector : {
        *(.boot)
    }

    . = 0x7C00 + 510;
    .mbr_sig : {
        *(.mbr_signature)
    }

    /* Sector 2+: The Kernel Arena */
    .text 0x9000 : AT(0x7C00 + 512) {
        *(.kernel_entry)    /* 1. Assembly entry wrapper code first */
        bin/kernel.o(.text*) /* 2. Force kernel.o code directly second */
        *(.text*)           /* 3. All remaining helper code modules (cykastd.o) */
    }

    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }
    .bss    : { *(.bss*) }
}
