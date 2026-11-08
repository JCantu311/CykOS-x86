ENTRY(_start)
OUTPUT_FORMAT("binary")

SECTIONS
{
    /* Everything starts assembling directly at 0x7C00 memory location layout */
    . = 0x7C00;

    .text : {
        bin/boot.o(.text*)   /* Sector 1 MBR & Sector 2 32-bit landing gear combined */
        bin/kernel.o(.text*) /* C kernel entry and routine methods */
        *(.text*)
    }

    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }
    .bss    : { *(.bss*) }
}
