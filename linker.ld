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
    /* This section starts exactly at file offset 512 bytes, but */
    /* targets execution mapping inside memory address 0x9000 */
    . = 0x9000;
    .text : AT(0x7C00 + 512) {
        *(.text*)
        *(.rodata*)
        *(.data*)
        *(.bss*)
    }
}
