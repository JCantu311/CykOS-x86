OUTPUT_FORMAT("binary")

SECTIONS
{
    . = __MODULE_LINK_BASE__;

    .text : {
        *(.header*)              /* cyk_module_header_t MUST be at byte 0 */
        *(.text*)                /* Driver routines */
        . = ALIGN(4);
        *(.rodata*)              /* Constants, lookup tables, and strings */
        *(.rdata*)
        . = ALIGN(4);
        *(.data*)                /* Driver state */
        *(.data.rel.ro*)
        *(.data.rel.ro.local*)
    }

    .bss (NOLOAD) : {
        . = ALIGN(4);
        *(.bss*)
        *(COMMON)
    }

    /DISCARD/ : {
        *(.eh_frame)
        *(.note*)
        *(.comment)
    }
}
