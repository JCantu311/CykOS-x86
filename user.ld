OUTPUT_FORMAT("binary")

SECTIONS
{
    /* User programs load at USER_APP_BASE (0x50000) */
    . = 0x50000;

    .text : {
        /* Entry point first so entry at 0x50000 jumps directly to it */
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
