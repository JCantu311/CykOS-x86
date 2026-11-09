#include "idt.h"
#include "cykastd.h"

#define IDT_ENTRIES 256

static idt_entry_t g_idt[IDT_ENTRIES];
static idt_ptr_t   g_idtp;
static isr_handler_t g_interrupt_handlers[IDT_ENTRIES];

// Names of standard x86 CPU exceptions (0..31)
static const char* const g_exception_messages[32] = {
    "Division by Zero",
    "Debug",
    "Non-Maskable Interrupt",
    "Breakpoint",
    "Into Detected Overflow",
    "Out of Bounds",
    "Invalid Opcode",
    "No Coprocessor",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Bad TSS",
    "Segment Not Present",
    "Stack Fault",
    "General Protection Fault",
    "Page Fault",
    "Unknown Interrupt",
    "Coprocessor Fault",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point Exception",
    "Virtualization Exception",
    "Control Protection Exception",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved"
};

void idt_set_gate(unsigned char num, unsigned int base, unsigned short sel, unsigned char flags) {
    g_idt[num].offset_low  = (base & 0xFFFF);
    g_idt[num].offset_high = (base >> 16) & 0xFFFF;
    g_idt[num].selector    = sel;
    g_idt[num].zero        = 0;
    g_idt[num].type_attr   = flags;
}

void register_interrupt_handler(unsigned char n, isr_handler_t handler) {
    g_interrupt_handlers[n] = handler;
}

// Print 32-bit hex value to VGA screen
static void print_hex32(unsigned int n) {
    const char hex[] = "0123456789ABCDEF";
    char buf[11];
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 7; i >= 0; i--) {
        buf[2 + (7 - i)] = hex[(n >> (i * 4)) & 0x0F];
    }
    buf[10] = '\0';
    print_string(buf);
}

// Common exception dispatcher called by all CPU exception ISRs
void fault_handler(registers_t* regs) {
    // If a custom handler is registered (e.g. syscall 0x80), invoke it
    if (g_interrupt_handlers[regs->int_no] != (void*)0) {
        g_interrupt_handlers[regs->int_no](regs);
        return;
    }

    // Unhandled CPU Exception: Display Blue Screen / Crash Dump
    clear_screen();
    print_color_string("================================================================================\n", VGA_RED);
    print_color_string("                    CYKOS KERNEL PANIC: UNHANDLED EXCEPTION                     \n", VGA_RED);
    print_color_string("================================================================================\n\n", VGA_RED);

    print_color_string(" Exception: ", VGA_WHITE);
    if (regs->int_no < 32) {
        print_color_string(g_exception_messages[regs->int_no], VGA_RED);
    } else {
        print_color_string("Unknown Vector", VGA_RED);
    }
    print_string(" (Interrupt ");
    print_hex32(regs->int_no);
    print_string(")\n");

    print_string(" Error Code: ");
    print_hex32(regs->err_code);
    print_string("\n\n");

    print_color_string(" CPU Registers:\n", VGA_GREEN);
    print_string("  EIP: "); print_hex32(regs->eip);
    print_string("  CS:  "); print_hex32(regs->cs);
    print_string("  EFLAGS: "); print_hex32(regs->eflags);
    print_string("\n");

    print_string("  EAX: "); print_hex32(regs->eax);
    print_string("  EBX: "); print_hex32(regs->ebx);
    print_string("  ECX:    "); print_hex32(regs->ecx);
    print_string("  EDX:    "); print_hex32(regs->edx);
    print_string("\n");

    print_string("  ESI: "); print_hex32(regs->esi);
    print_string("  EDI: "); print_hex32(regs->edi);
    print_string("  EBP:    "); print_hex32(regs->ebp);
    print_string("  ESP:    "); print_hex32(regs->useresp);
    print_string("\n\n");

    print_color_string("System halted to prevent memory corruption. Please reboot your computer.\n", VGA_WHITE);

    // Halt the CPU indefinitely
    while (1) {
        asm volatile("cli; hlt");
    }
}

// Low-level assembly ISR stubs generated via GNU Assembler macros
extern void isr0(void);  extern void isr1(void);  extern void isr2(void);  extern void isr3(void);
extern void isr4(void);  extern void isr5(void);  extern void isr6(void);  extern void isr7(void);
extern void isr8(void);  extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void); extern void isr15(void);
extern void isr16(void); extern void isr17(void); extern void isr18(void); extern void isr19(void);
extern void isr20(void); extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void); extern void isr27(void);
extern void isr28(void); extern void isr29(void); extern void isr30(void); extern void isr31(void);
extern void isr128(void); // Vector 0x80: Syscall Gate

// Inline assembly defining the raw ISR stubs
__asm__(
    ".text\n"
    ".align 4\n"

    // Macro for exceptions without CPU error code (pushes dummy 0)
    ".macro ISR_NOERRCODE num\n"
    ".global isr\\num\n"
    "isr\\num:\n"
    "    pushl $0\n"          // Dummy error code
    "    pushl $\\num\n"       // Interrupt number
    "    jmp isr_common_stub\n"
    ".endm\n"

    // Macro for exceptions WITH CPU error code (error code already pushed by CPU)
    ".macro ISR_ERRCODE num\n"
    ".global isr\\num\n"
    "isr\\num:\n"
    "    pushl $\\num\n"       // Interrupt number
    "    jmp isr_common_stub\n"
    ".endm\n"

    // Generate ISRs 0..31
    "ISR_NOERRCODE 0\n"
    "ISR_NOERRCODE 1\n"
    "ISR_NOERRCODE 2\n"
    "ISR_NOERRCODE 3\n"
    "ISR_NOERRCODE 4\n"
    "ISR_NOERRCODE 5\n"
    "ISR_NOERRCODE 6\n"
    "ISR_NOERRCODE 7\n"
    "ISR_ERRCODE   8\n"   // Double Fault
    "ISR_NOERRCODE 9\n"
    "ISR_ERRCODE   10\n"  // Invalid TSS
    "ISR_ERRCODE   11\n"  // Segment Not Present
    "ISR_ERRCODE   12\n"  // Stack Fault
    "ISR_ERRCODE   13\n"  // General Protection Fault
    "ISR_ERRCODE   14\n"  // Page Fault
    "ISR_NOERRCODE 15\n"
    "ISR_NOERRCODE 16\n"
    "ISR_ERRCODE   17\n"  // Alignment Check
    "ISR_NOERRCODE 18\n"
    "ISR_NOERRCODE 19\n"
    "ISR_NOERRCODE 20\n"
    "ISR_NOERRCODE 21\n"
    "ISR_NOERRCODE 22\n"
    "ISR_NOERRCODE 23\n"
    "ISR_NOERRCODE 24\n"
    "ISR_NOERRCODE 25\n"
    "ISR_NOERRCODE 26\n"
    "ISR_NOERRCODE 27\n"
    "ISR_NOERRCODE 28\n"
    "ISR_NOERRCODE 29\n"
    "ISR_NOERRCODE 30\n"
    "ISR_NOERRCODE 31\n"

    // Syscall 0x80 (128)
    "ISR_NOERRCODE 128\n"

    // Common landing stub: saves full CPU state, sets kernel segments, calls C dispatcher
    "isr_common_stub:\n"
    "    pusha\n"
    "    movw %ds, %ax\n"
    "    pushl %eax\n"
    "    movw $0x10, %ax\n"   // Load Kernel Data Segment (0x10)
    "    movw %ax, %ds\n"
    "    movw %ax, %es\n"
    "    movw %ax, %fs\n"
    "    movw %ax, %gs\n"
    "    pushl %esp\n"        // Pass pointer to registers_t as argument
    "    call fault_handler\n"
    "    addl $4, %esp\n"
    "    popl %eax\n"
    "    movw %ax, %ds\n"
    "    movw %ax, %es\n"
    "    movw %ax, %fs\n"
    "    movw %ax, %gs\n"
    "    popa\n"
    "    addl $8, %esp\n"     // Clean up pushed error code and interrupt number
    "    iret\n"
);

void idt_init(void) {
    g_idtp.limit = (sizeof(idt_entry_t) * IDT_ENTRIES) - 1;
    g_idtp.base  = (unsigned int)&g_idt;

    memset(&g_idt, 0, sizeof(g_idt));
    memset(&g_interrupt_handlers, 0, sizeof(g_interrupt_handlers));

    // Install CPU Exception Handlers (0..31) - Ring 0 interrupt gates (0x8E)
    idt_set_gate(0,  (unsigned int)isr0,  0x08, 0x8E);
    idt_set_gate(1,  (unsigned int)isr1,  0x08, 0x8E);
    idt_set_gate(2,  (unsigned int)isr2,  0x08, 0x8E);
    idt_set_gate(3,  (unsigned int)isr3,  0x08, 0x8E);
    idt_set_gate(4,  (unsigned int)isr4,  0x08, 0x8E);
    idt_set_gate(5,  (unsigned int)isr5,  0x08, 0x8E);
    idt_set_gate(6,  (unsigned int)isr6,  0x08, 0x8E);
    idt_set_gate(7,  (unsigned int)isr7,  0x08, 0x8E);
    idt_set_gate(8,  (unsigned int)isr8,  0x08, 0x8E);
    idt_set_gate(9,  (unsigned int)isr9,  0x08, 0x8E);
    idt_set_gate(10, (unsigned int)isr10, 0x08, 0x8E);
    idt_set_gate(11, (unsigned int)isr11, 0x08, 0x8E);
    idt_set_gate(12, (unsigned int)isr12, 0x08, 0x8E);
    idt_set_gate(13, (unsigned int)isr13, 0x08, 0x8E);
    idt_set_gate(14, (unsigned int)isr14, 0x08, 0x8E);
    idt_set_gate(15, (unsigned int)isr15, 0x08, 0x8E);
    idt_set_gate(16, (unsigned int)isr16, 0x08, 0x8E);
    idt_set_gate(17, (unsigned int)isr17, 0x08, 0x8E);
    idt_set_gate(18, (unsigned int)isr18, 0x08, 0x8E);
    idt_set_gate(19, (unsigned int)isr19, 0x08, 0x8E);
    idt_set_gate(20, (unsigned int)isr20, 0x08, 0x8E);
    idt_set_gate(21, (unsigned int)isr21, 0x08, 0x8E);
    idt_set_gate(22, (unsigned int)isr22, 0x08, 0x8E);
    idt_set_gate(23, (unsigned int)isr23, 0x08, 0x8E);
    idt_set_gate(24, (unsigned int)isr24, 0x08, 0x8E);
    idt_set_gate(25, (unsigned int)isr25, 0x08, 0x8E);
    idt_set_gate(26, (unsigned int)isr26, 0x08, 0x8E);
    idt_set_gate(27, (unsigned int)isr27, 0x08, 0x8E);
    idt_set_gate(28, (unsigned int)isr28, 0x08, 0x8E);
    idt_set_gate(29, (unsigned int)isr29, 0x08, 0x8E);
    idt_set_gate(30, (unsigned int)isr30, 0x08, 0x8E);
    idt_set_gate(31, (unsigned int)isr31, 0x08, 0x8E);

    // Install Syscall Handler at 0x80 (128) - 0xEE sets DPL=3 so userland can call 'int 0x80'
    idt_set_gate(128, (unsigned int)isr128, 0x08, 0xEE);

    // Load IDT into CPU register
    asm volatile("lidt %0" : : "m"(g_idtp));
}
