ENTRY(_start)
OUTPUT_FORMAT("binary")

SECTIONS
{
    /* Sector 1: MBR Code Space */
    . = 0x7C00;
    
    .boot_sector : {
        bin/boot.o(.boot)
    }

    . = 0x7C00 + 510;
    .mbr_sig : {
        bin/boot.o(.mbr_signature)
    }

    /* Sector 2+: Placed immediately at byte 512, executes at 0x9000 */
    .text 0x9000 : AT(0x7C00 + 512) {
        bin/kstart.o(.kstart) /* 1. Force the isolated 32-bit entry code first */
        bin/kernel.o(.text*)  /* 2. Place kernel_main second */
        *(.text*)             /* 3. Place remaining helper functions third */
    }

    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }
    .bss    : { *(.bss*) }
}
