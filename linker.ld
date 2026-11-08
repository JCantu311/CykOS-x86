ENTRY(_start)
OUTPUT_FORMAT("binary")

SECTIONS
{
    /* Sector 1: Physically starts at file offset 0, executes at 0x7C00 */
    . = 0x7C00;
    
    .boot_sector : {
        *(.boot)
    }

    . = 0x7C00 + 510;
    .mbr_sig : {
        *(.mbr_signature)
    }

    /* 
     * Sector 2+: Placed immediately at file offset 512 (Load Address),
     * but compiled to run at memory address 0x9000 (Virtual Address).
     */
    .text 0x9000 : AT(0x7C00 + 512) {
        *(.kernel_entry)
        *(.text*)
    }

    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }
    .bss    : { *(.bss*) }
}
