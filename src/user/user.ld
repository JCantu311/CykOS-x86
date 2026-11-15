OUTPUT_FORMAT("binary")
ENTRY(main)

SECTIONS
{
    /* User programs load after the reserved Level 2 module slots. */
    . = 0x180000;

    .text : {
        /* Entry point MUST be at byte 0 — CPU jumps here directly */
        KEEP(*(.entry))
        *(.text.user_edit)
        *(.text.main)
        *(.text._start)
        *(.text*)
        . = ALIGN(4);
        *(.rodata*)
        *(.rdata*)
        . = ALIGN(4);
        *(.data*)
        *(.data.rel.ro*)
        *(.data.rel.ro.local*)
        *(.got*)
        *(.got.plt*)
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
