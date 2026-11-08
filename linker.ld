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
     * Sector 2+: Placed contiguously at file offset 512,
     * but compiled to execute at memory address 0x9000.
     */
    .text 0x9000 : AT(0x7C00 + 512) {
        *(.kernel_entry)    /* 1. Assembly entry wrapper code first */
        *(.kernel_start)    /* 2. Your kernel_main function second */
        *(.text*)           /* 3. All other helper functions and code modules */
    }

    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }
    .bss    : { *(.bss*) }
}
