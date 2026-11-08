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

    /* 
     * CRITICAL FIX: Advance the location counter to 0x9000. 
     * In a flat binary output, the linker will automatically pad 
     * the gap with zeroes, perfectly aligning Sector 2 in the file 
     * with address 0x9000 in memory.
     */
    . = 0x9000;
    
    .text : {
        *(.kernel_entry)
        *(.text*)
    }

    /* Read-only data, globals, and bss follow sequentially */
    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }
    .bss    : { *(.bss*) }
}
