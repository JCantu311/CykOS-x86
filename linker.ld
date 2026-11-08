ENTRY(_start)
OUTPUT_FORMAT("binary")

SECTIONS
{
    /* Sector 1: Explicit MBR boundary */
    . = 0x7C00;
    
    .boot_sector : {
        *(.boot)
    }

    /* Target the exact hardware MBR signature block offset */
    . = 0x7C00 + 510;
    .mbr_sig : {
        *(.mbr_signature)
    }

    /* Sector 2+: The Kernel Arena */
    . = 0x9000;
    
    /* Code section sits perfectly at the front of the load zone */
    .text : AT(0x7C00 + 512) {
        *(.text*)
    }

    /* Read-only data, globals, and bss follow sequentially */
    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }
    .bss    : { *(.bss*) }
}
