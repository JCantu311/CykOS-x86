#ifndef CYKOS_GDT_H
#define CYKOS_GDT_H

#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_USER_CODE   0x1B
#define GDT_USER_DATA   0x23
#define GDT_TSS         0x28

void gdt_init(void);
void gdt_set_kernel_stack(unsigned int esp0);
unsigned int gdt_get_kernel_stack(void);

#endif
