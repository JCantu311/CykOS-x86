OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(main)

PHDRS
{
    app PT_LOAD FLAGS(7);
}

SECTIONS
{
    . = 0x180000;
    .text : {
        KEEP(*(.entry))
        *(.text.main)
        *(.text*)
        *(.rodata*)
        *(.rdata*)
    } :app
    . = ALIGN(4);
    .data : {
        *(.data*)
        *(.data.rel.ro*)
        *(.data.rel.ro.local*)
        *(.got*)
        *(.got.plt*)
    } :app
    .bss (NOLOAD) : {
        *(.bss*)
        *(COMMON)
    } :app

    /DISCARD/ : {
        *(.dynamic*)
        *(.dynsym*)
        *(.dynstr*)
        *(.rel.dyn*)
        *(.rel.plt*)
        *(.interp*)
        *(.eh_frame*)
        *(.gcc_except_table*)
        *(.note*)
        *(.comment)
    }
}
