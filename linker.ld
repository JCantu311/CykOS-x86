ENTRY(_start)
OUTPUT_FORMAT("binary")

SECTIONS
{
    . = 0x7C00;

    .text : {
        *(.boot)
        *(.text*)
    }

    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }
    .bss    : { *(.bss*) }

    . = 0x7C00 + 510;
    .mbr_sig : {
        *(.mbr_signature)
    }
}
