ENTRY(init_32bit_protected)
OUTPUT_FORMAT("binary")
INCLUDE config/memory_layout.ld

__shell_image_base = SHELL_IMAGE_BASE;
__shell_image_limit = SHELL_IMAGE_END;

SECTIONS
{
    /* The independent BIOS loader places this kernel file at 0x10000. */
    . = 0x10000;
    _kernel_start = .;

    .text : {
        KEEP(*boot.o(.text*))    /* 32-bit assembly entry landing code goes FIRST */
        KEEP(*idt.o(.text*))     /* Exception handlers and ISRs */
        KEEP(*syscall.o(.text*)) /* Syscall dispatcher */
        KEEP(*heap.o(.text*))    /* Dynamic memory allocator */
        KEEP(*cykastd.o(.text*)) /* Retain all standard library & I/O functions for apps */
        KEEP(*fat16.o(.text*))   /* Retain all FAT16 read/write routines for apps */
        *(.text*)                /* All executive code routines */
        . = ALIGN(4);
        KEEP(*idt.o(.rodata*))
        KEEP(*syscall.o(.rodata*))
        KEEP(*heap.o(.rodata*))
        KEEP(*cykastd.o(.rodata*))
        KEEP(*fat16.o(.rodata*))
        *(.rodata*)              /* Global string literals and constants */
        *(.rdata*)               /* Read-only data for MinGW/PE-COFF toolchains */
        . = ALIGN(4);
        KEEP(*idt.o(.data*))
        KEEP(*syscall.o(.data*))
        KEEP(*heap.o(.data*))
        KEEP(*cykastd.o(.data*))
        KEEP(*fat16.o(.data*))
        *(.data*)                /* Initialized global variables */
        *(.data.rel.ro*)         /* Initialized pointers to read-only strings */
        *(.data.rel.ro.local*)
        *(.got*)
        *(.got.plt*)
    }

    . = ALIGN(4);
    _kernel_end = .;
    _kernel_size = _kernel_end - _kernel_start;
    _kernel_sectors = (_kernel_size + 511) / 512;

    .bss (NOLOAD) : {
        . = ALIGN(4);
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    }

    ASSERT(_kernel_end <= 0x90000, "Kernel image overlaps the driver arena")
    ASSERT(__bss_end <= 0x90000, "Kernel and BSS overlap the driver arena")
    ASSERT(init_32bit_protected == _kernel_start,
        "Kernel file must begin with the protected-mode entry")

    /DISCARD/ : {
        *(.boot)
        *(.gcc_except_table*)
        *(.eh_frame*)
        *(.note*)
        *(.comment)
        *(.debug*)
    }
}
