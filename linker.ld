ENTRY(_start)
OUTPUT_FORMAT("binary")

SECTIONS
{
    /* Sector 1: Boot Sector begins at address 0x7C00 */
    . = 0x7C00;
    .boot : {
        bin/boot.o(.text*)
    }

    /* Sector 2+: Placed immediately at byte 512, compiled to execute at 0x9000 */
    .text 0x9000 : AT(0x7C00 + 512) {
        bin/kstart.o(.kstart)
        bin/kernel.o(.text*)
        *(.text*)
    }

    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }
    .bss    : { *(.bss*) }
}
