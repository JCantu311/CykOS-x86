OUTPUT_FORMAT("binary")
ENTRY(main)

SECTIONS
{
    /* User applications load after the reserved Level 2 module slots. */
    . = 0x68000;

    .text : {
        /* Entry point MUST be at byte 0 of the binary */
        KEEP(*(.entry))
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
