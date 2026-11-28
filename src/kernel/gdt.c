#include "gdt.h"

typedef struct __attribute__((packed)) {
    unsigned short limit_low;
    unsigned short base_low;
    unsigned char base_middle;
    unsigned char access;
    unsigned char granularity;
    unsigned char base_high;
} gdt_entry_t;

typedef struct __attribute__((packed)) {
    unsigned short limit;
    unsigned int base;
} gdt_pointer_t;

typedef struct __attribute__((packed)) {
    unsigned short previous_task, reserved0;
    unsigned int esp0;
    unsigned short ss0, reserved1;
    unsigned int esp1;
    unsigned short ss1, reserved2;
    unsigned int esp2;
    unsigned short ss2, reserved3;
    unsigned int cr3, eip, eflags;
    unsigned int eax, ecx, edx, ebx, esp, ebp, esi, edi;
    unsigned short es, reserved4;
    unsigned short cs, reserved5;
    unsigned short ss, reserved6;
    unsigned short ds, reserved7;
    unsigned short fs, reserved8;
    unsigned short gs, reserved9;
    unsigned short ldt, reserved10;
    unsigned short trap;
    unsigned short iomap_base;
} tss32_t;

static gdt_entry_t g_gdt[6];
static gdt_pointer_t g_gdt_pointer;
static tss32_t g_tss;
static unsigned char g_kernel_interrupt_stack[16384] __attribute__((aligned(16)));

static void gdt_set_entry(unsigned int index, unsigned int base,
                          unsigned int limit, unsigned char access,
                          unsigned char granularity) {
    g_gdt[index].base_low = (unsigned short)(base & 0xFFFFu);
    g_gdt[index].base_middle = (unsigned char)((base >> 16) & 0xFFu);
    g_gdt[index].base_high = (unsigned char)((base >> 24) & 0xFFu);
    g_gdt[index].limit_low = (unsigned short)(limit & 0xFFFFu);
    g_gdt[index].granularity = (unsigned char)(((limit >> 16) & 0x0Fu) |
                                                (granularity & 0xF0u));
    g_gdt[index].access = access;
}

void gdt_set_kernel_stack(unsigned int esp0) {
    g_tss.esp0 = esp0;
}

unsigned int gdt_get_kernel_stack(void) {
    return g_tss.esp0;
}

void gdt_init(void) {
    gdt_set_entry(0, 0, 0, 0, 0);
    gdt_set_entry(1, 0, 0xFFFFFFFFu, 0x9A, 0xCF); /* Ring 0 code */
    gdt_set_entry(2, 0, 0xFFFFFFFFu, 0x92, 0xCF); /* Ring 0 data */
    gdt_set_entry(3, 0, 0xFFFFFFFFu, 0xFA, 0xCF); /* Ring 3 code */
    gdt_set_entry(4, 0, 0xFFFFFFFFu, 0xF2, 0xCF); /* Ring 3 data */

    g_tss.ss0 = GDT_KERNEL_DATA;
    g_tss.esp0 = (unsigned int)(g_kernel_interrupt_stack + sizeof(g_kernel_interrupt_stack));
    g_tss.iomap_base = sizeof(g_tss); /* No user I/O bitmap: port access is denied. */
    gdt_set_entry(5, (unsigned int)&g_tss, sizeof(g_tss) - 1u, 0x89, 0x00);

    g_gdt_pointer.limit = sizeof(g_gdt) - 1u;
    g_gdt_pointer.base = (unsigned int)g_gdt;
    __asm__ volatile(
        "lgdt (%0)\n\t"
        "ljmp $0x08, $1f\n"
        "1:\n\t"
        "movw $0x10, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        "movw %%ax, %%ss\n\t"
        "movw $0x28, %%ax\n\t"
        "ltr %%ax\n"
        : : "r"(&g_gdt_pointer) : "eax", "memory");
}
