OUTPUT_FORMAT("binary")
ENTRY(main)

SECTIONS
{
    . = 0x100000;

    .text : {
        KEEP(*(.entry))
        *(.text.main)
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
        *(.gcc_except_table*)
        *(.eh_frame*)
        *(.note*)
        *(.comment)
    }
}