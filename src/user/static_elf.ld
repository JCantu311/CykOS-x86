OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(main)

PHDRS
{
    text PT_LOAD FLAGS(5); /* Read + execute */
    data PT_LOAD FLAGS(6); /* Read + write */
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
    } :text
    . = ALIGN(0x1000);
    .data : {
        *(.data*)
        *(.data.rel.ro*)
        *(.data.rel.ro.local*)
        *(.got*)
        *(.got.plt*)
    } :data
    .bss (NOLOAD) : {
        *(.bss*)
        *(COMMON)
    } :data

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
