#ifndef IDT_H
#define IDT_H

// Raw 8-byte IDT entry (Gate Descriptor)
typedef struct __attribute__((packed)) {
    unsigned short offset_low;   // Bits 0..15 of handler function address
    unsigned short selector;     // Code segment selector (0x08 = Kernel Code)
    unsigned char  zero;         // Unused, must be 0
    unsigned char  type_attr;    // Gate type, DPL, Present bit (e.g. 0x8E = Interrupt Gate, 0xEE = User Syscall)
    unsigned short offset_high;  // Bits 16..31 of handler function address
} idt_entry_t;

// Structure loaded into CPU via LIDT instruction
typedef struct __attribute__((packed)) {
    unsigned short limit;        // Total size of IDT table minus 1 (256 * 8 - 1)
    unsigned int   base;         // Base address of idt_entry_t array
} idt_ptr_t;

// Register state captured on stack during interrupt / exception
typedef struct __attribute__((packed)) {
    unsigned int ds;                                     // Data segment selector
    unsigned int edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax; // Pushed by pusha
    unsigned int int_no, err_code;                      // Interrupt number and error code
    unsigned int eip, cs, eflags, useresp, ss;          // Pushed automatically by CPU
} registers_t;

typedef void (*isr_handler_t)(registers_t* regs);

// Public Functions
void idt_init(void);
void idt_set_gate(unsigned char num, unsigned int base, unsigned short sel, unsigned char flags);
void register_interrupt_handler(unsigned char n, isr_handler_t handler);

#endif // IDT_H
