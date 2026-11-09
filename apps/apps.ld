OUTPUT_FORMAT("binary")

SECTIONS
{
    /* User applications in CykOS load at 0x50000 */
    . = 0x50000;

    .text : {
        /* Entry point MUST be at byte 0 of the binary */
        *(.text.main)
        *(.text.user_edit)
        *(.text._start)
        *(.text*)
        . = ALIGN(4);
        *(.rodata*)
        *(.rdata*)
        . = ALIGN(4);
        *(.data*)
        *(.data.rel.ro*)
        *(.data.rel.ro.local*)
    }

    .bss (NOLOAD) : {
        . = ALIGN(4);
        *(.bss*)
        *(COMMON)
    }

    /DISCARD/ : {
        *(.eh_frame*)
        *(.note*)
        *(.comment)
    }
}
