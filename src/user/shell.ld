OUTPUT_FORMAT("binary")
ENTRY(main)
INCLUDE config/memory_layout.ld

SECTIONS
{
    . = SHELL_IMAGE_BASE;
    __shell_image_base = .;

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

    __shell_image_end = .;
    __shell_image_limit = SHELL_IMAGE_END;
    ASSERT(__shell_image_end <= __shell_image_limit,
        "Standalone shell exceeds its reserved memory region")

    /DISCARD/ : {
        *(.gcc_except_table*)
        *(.eh_frame*)
        *(.note*)
        *(.comment)
    }
}
