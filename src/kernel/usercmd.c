#include "usercmd.h"
#include "vfs.h"
#include "cykastd.h"
#include "heap.h"
#include "syscall.h"
#include "modload.h"
#include "exports.h"
#include "paging.h"
#include "gdt.h"

#define USERCMDS_FILE     "/ETC/USERCMDS.LST"
#define USERCMDS_FALLBACK "/ETC/USERCMDS.TXT"
#define USERCMDS_LEGACY   "/CYKOS/USERCMDS.LST"
#define USERCMDS_OLD_FALLBACK "/CYKOS/USERCMDS.TXT"
#define USER_BIN_DIR      "/USR/BIN/"
#define LEGACY_BIN_DIR    "/CYKOS/"
#define USER_APP_BASE     0x180000  // RAM arena above the BIOS/VGA memory hole
#define USER_APP_MAX_SIZE 0x90000   // 576 KB, fixed app arena ends at 0x210000
#define USER_DYNAMIC_APP_BASE 0x300000
#define USER_DYNAMIC_APP_MAX_SIZE 0xC0000
#define USER_SHELL_STACK_BASE 0x210000
#define USER_SHELL_STACK_SIZE 0x20000
#define USER_APP_STACK_BASE 0x250000
#define USER_APP_STACK_SIZE 0x40000 // Doom and other foreground apps get a 256 KiB stack.
#define USER_APP_STACK_GUARD_SIZE 16
#define USER_APP_STACK_GUARD_VALUE 0xC7A55AFEu
#define USER_APP_HEAP_MAX_SIZE 0x1000000 // Bound app-owned heap arena to 16 MiB
#define USER_APP_HEAP_BASE 0x800000
#define USER_APP_FILE_MAX 0x80000   // ELF file may include headers and dynamic tables
#define USER_PATH_MAX     64
static volatile int g_usercmd_cancel_requested;
static volatile int g_usercmd_abort_requested;

/* Absolute linker symbols keep the flat shell at its linked address while
 * sharing the reserved image window with src/user/shell.ld. */
extern unsigned char __shell_image_base[];
extern unsigned char __shell_image_limit[];

#define ELF_ET_DYN        3
#define ELF_ET_EXEC       2
#define ELF_EM_386        3
#define ELF_PT_LOAD       1
#define ELF_PT_DYNAMIC    2
#define ELF_PF_X          1
#define ELF_DT_NULL       0
#define ELF_DT_NEEDED     1
#define ELF_DT_HASH       4
#define ELF_DT_STRTAB     5
#define ELF_DT_SYMTAB     6
#define ELF_DT_STRSZ      10
#define ELF_DT_SYMENT     11
#define ELF_DT_REL        17
#define ELF_DT_RELSZ      18
#define ELF_DT_RELENT     19
#define ELF_DT_PLTREL     20
#define ELF_DT_JMPREL     23
#define ELF_DT_PLTRELSZ   2
#define ELF_SHN_UNDEF     0
#define ELF_SHN_ABS       0xFFF1
#define ELF_STB_WEAK      2
#define ELF_R_386_NONE    0
#define ELF_R_386_32      1
#define ELF_R_386_PC32    2
#define ELF_R_386_GLOB_DAT 6
#define ELF_R_386_JMP_SLOT 7
#define ELF_R_386_RELATIVE 8

typedef struct __attribute__((packed)) {
    unsigned char ident[16];
    unsigned short type;
    unsigned short machine;
    unsigned int version;
    unsigned int entry;
    unsigned int phoff;
    unsigned int shoff;
    unsigned int flags;
    unsigned short ehsize;
    unsigned short phentsize;
    unsigned short phnum;
    unsigned short shentsize;
    unsigned short shnum;
    unsigned short shstrndx;
} elf32_ehdr_t;

typedef struct __attribute__((packed)) {
    unsigned int type;
    unsigned int offset;
    unsigned int vaddr;
    unsigned int paddr;
    unsigned int filesz;
    unsigned int memsz;
    unsigned int flags;
    unsigned int align;
} elf32_phdr_t;

typedef struct __attribute__((packed)) {
    unsigned int tag;
    unsigned int value;
} elf32_dyn_t;

typedef struct __attribute__((packed)) {
    unsigned int offset;
    unsigned int info;
} elf32_rel_t;

typedef struct __attribute__((packed)) {
    unsigned int name;
    unsigned int value;
    unsigned int size;
    unsigned char info;
    unsigned char other;
    unsigned short shndx;
} elf32_sym_t;

static int image_range(unsigned int vaddr, unsigned int length,
                       unsigned int min_vaddr, unsigned int max_vaddr) {
    return vaddr >= min_vaddr && vaddr <= max_vaddr && length <= max_vaddr - vaddr;
}

static int zero_elf_bss(const elf32_phdr_t* ph, unsigned int count,
                        unsigned int load_bias) {
    for (unsigned int i = 0; i < count; i++) {
        if (ph[i].type != ELF_PT_LOAD || ph[i].memsz <= ph[i].filesz) continue;
        unsigned int bss_start = load_bias + ph[i].vaddr + ph[i].filesz;
        unsigned int remaining = ph[i].memsz - ph[i].filesz;
        while (remaining) {
            if (g_usercmd_cancel_requested) return -1;
            unsigned int chunk = remaining > 4096u ? 4096u : remaining;
            memset((void*)bss_start, 0, chunk);
            bss_start += chunk;
            remaining -= chunk;
        }
    }
    return 0;
}

static int copy_elf_load_segments(const unsigned char* file,
                                  const elf32_phdr_t* ph, unsigned int count,
                                  unsigned int load_bias) {
    for (unsigned int i = 0; i < count; i++) {
        if (ph[i].type != ELF_PT_LOAD || !ph[i].memsz) continue;
        unsigned int destination = load_bias + ph[i].vaddr;
        unsigned int source = ph[i].offset;
        unsigned int remaining = ph[i].filesz;
        while (remaining) {
            if (g_usercmd_cancel_requested) return -1;
            unsigned int chunk = remaining > 4096u ? 4096u : remaining;
            memcpy((void*)destination, file + source, chunk);
            destination += chunk;
            source += chunk;
            remaining -= chunk;
        }
    }
    return 0;
}

static int resolve_elf_relocations(unsigned int load_bias,
                                   unsigned int min_vaddr, unsigned int max_vaddr,
                                   unsigned int rel_vaddr, unsigned int rel_size,
                                   unsigned int rel_entry_size,
                                   unsigned int sym_vaddr, unsigned int sym_count,
                                   unsigned int str_vaddr, unsigned int str_size,
                                   int* has_kernel_import) {
    if (rel_size == 0) return 0;
    if (rel_entry_size != sizeof(elf32_rel_t) || rel_size % rel_entry_size != 0 ||
        !image_range(rel_vaddr, rel_size, min_vaddr, max_vaddr)) return -1;

    elf32_rel_t* rels = (elf32_rel_t*)(load_bias + rel_vaddr);
    elf32_sym_t* symbols = (elf32_sym_t*)(load_bias + sym_vaddr);
    const char* strings = (const char*)(load_bias + str_vaddr);
    unsigned int count = rel_size / rel_entry_size;
    for (unsigned int i = 0; i < count; i++) {
        unsigned int type = rels[i].info & 0xFF;
        unsigned int symbol_index = rels[i].info >> 8;
        if (type == ELF_R_386_NONE) continue;
        if (!image_range(rels[i].offset, sizeof(unsigned int), min_vaddr, max_vaddr)) return -1;
        unsigned int* place = (unsigned int*)(load_bias + rels[i].offset);
        if (type == ELF_R_386_RELATIVE) {
            *place += load_bias;
            continue;
        }
        if (type != ELF_R_386_32 && type != ELF_R_386_PC32 &&
            type != ELF_R_386_GLOB_DAT && type != ELF_R_386_JMP_SLOT) return -1;
        if (symbol_index >= sym_count) return -1;

        elf32_sym_t* symbol = &symbols[symbol_index];
        unsigned int symbol_address;
        if (symbol->shndx == ELF_SHN_UNDEF) {
            if (symbol->name >= str_size) return -1;
            unsigned int remain = str_size - symbol->name;
            unsigned int n = 0;
            while (n < remain && strings[symbol->name + n]) n++;
            if (n == remain) return -1;
            if (kernel_export_resolve(strings + symbol->name, &symbol_address) != 0) {
                if ((symbol->info >> 4) == ELF_STB_WEAK) symbol_address = 0;
                else return -1;
            } else if (has_kernel_import) *has_kernel_import = 1;
        } else if (symbol->shndx == ELF_SHN_ABS) {
            symbol_address = symbol->value;
        } else {
            if (symbol->value < min_vaddr || symbol->value >= max_vaddr) return -1;
            symbol_address = load_bias + symbol->value;
        }

        if (type == ELF_R_386_32) *place += symbol_address;
        else if (type == ELF_R_386_PC32) *place += symbol_address - (unsigned int)place;
        else *place = symbol_address;
    }
    return 0;
}

static int load_dynamic_elf32(const unsigned char* file, unsigned int file_size,
                              unsigned char* load_area, unsigned int* entry_out,
                              int* has_kernel_import) {
    const elf32_ehdr_t* eh = (const elf32_ehdr_t*)file;
    if (file_size < sizeof(*eh) || eh->ident[0] != 0x7F || eh->ident[1] != 'E' ||
        eh->ident[2] != 'L' || eh->ident[3] != 'F' || eh->ident[4] != 1 ||
        eh->ident[5] != 1 || eh->type != ELF_ET_DYN || eh->machine != ELF_EM_386 ||
        eh->phentsize != sizeof(elf32_phdr_t) || eh->phnum == 0 || eh->phnum > 32 ||
        eh->phoff > file_size || eh->phnum > (file_size - eh->phoff) / eh->phentsize) return -1;

    const elf32_phdr_t* ph = (const elf32_phdr_t*)(file + eh->phoff);
    unsigned int min_vaddr = 0xFFFFFFFFu;
    unsigned int max_vaddr = 0;
    unsigned int dynamic_vaddr = 0;
    unsigned int dynamic_size = 0;
    int executable_segment = 0;
    for (unsigned int i = 0; i < eh->phnum; i++) {
        if (ph[i].type == ELF_PT_DYNAMIC) {
            dynamic_vaddr = ph[i].vaddr;
            dynamic_size = ph[i].memsz;
        }
        if (ph[i].type != ELF_PT_LOAD) continue;
        if (ph[i].filesz > ph[i].memsz || ph[i].offset > file_size ||
            ph[i].filesz > file_size - ph[i].offset ||
            ph[i].vaddr > 0xFFFFFFFFu - ph[i].memsz) return -1;
        if (ph[i].vaddr < min_vaddr) min_vaddr = ph[i].vaddr;
        if (ph[i].vaddr + ph[i].memsz > max_vaddr) max_vaddr = ph[i].vaddr + ph[i].memsz;
        if (ph[i].flags & ELF_PF_X) executable_segment = 1;
    }
    if (min_vaddr == 0xFFFFFFFFu || max_vaddr <= min_vaddr ||
        min_vaddr > USER_APP_BASE || max_vaddr - min_vaddr > USER_DYNAMIC_APP_MAX_SIZE ||
        !executable_segment || !dynamic_vaddr || !dynamic_size ||
        !image_range(dynamic_vaddr, dynamic_size, min_vaddr, max_vaddr)) return -1;

    unsigned int load_base = (unsigned int)load_area;
    if (load_base < min_vaddr) return -1;
    unsigned int load_bias = load_base - min_vaddr;
    if (load_bias > 0xFFFFFFFFu - max_vaddr ||
        load_bias + max_vaddr > load_base + USER_DYNAMIC_APP_MAX_SIZE) return -1;

    if (zero_elf_bss(ph, eh->phnum, load_bias) != 0 ||
        copy_elf_load_segments(file, ph, eh->phnum, load_bias) != 0) return -1;

    unsigned int hash_vaddr = 0, sym_vaddr = 0, str_vaddr = 0;
    unsigned int str_size = 0, sym_entry_size = 0;
    unsigned int rel_vaddr = 0, rel_size = 0, rel_entry_size = sizeof(elf32_rel_t);
    unsigned int plt_rel_vaddr = 0, plt_rel_size = 0, plt_rel_type = 0;
    elf32_dyn_t* dyn = (elf32_dyn_t*)(load_bias + dynamic_vaddr);
    unsigned int dynamic_entries = dynamic_size / sizeof(elf32_dyn_t);
    int saw_null = 0;
    for (unsigned int i = 0; i < dynamic_entries; i++) {
        switch (dyn[i].tag) {
            case ELF_DT_NULL: saw_null = 1; i = dynamic_entries; break;
            case ELF_DT_NEEDED: return -1; // No dependent shared libraries yet.
            case ELF_DT_HASH: hash_vaddr = dyn[i].value; break;
            case ELF_DT_SYMTAB: sym_vaddr = dyn[i].value; break;
            case ELF_DT_STRTAB: str_vaddr = dyn[i].value; break;
            case ELF_DT_STRSZ: str_size = dyn[i].value; break;
            case ELF_DT_SYMENT: sym_entry_size = dyn[i].value; break;
            case ELF_DT_REL: rel_vaddr = dyn[i].value; break;
            case ELF_DT_RELSZ: rel_size = dyn[i].value; break;
            case ELF_DT_RELENT: rel_entry_size = dyn[i].value; break;
            case ELF_DT_JMPREL: plt_rel_vaddr = dyn[i].value; break;
            case ELF_DT_PLTRELSZ: plt_rel_size = dyn[i].value; break;
            case ELF_DT_PLTREL: plt_rel_type = dyn[i].value; break;
            default: break;
        }
    }
    if (!saw_null || !hash_vaddr || !sym_vaddr || !str_vaddr || !str_size ||
        sym_entry_size != sizeof(elf32_sym_t) ||
        !image_range(hash_vaddr, 8, min_vaddr, max_vaddr) ||
        !image_range(sym_vaddr, sizeof(elf32_sym_t), min_vaddr, max_vaddr) ||
        !image_range(str_vaddr, str_size, min_vaddr, max_vaddr)) return -1;

    unsigned int* hash = (unsigned int*)(load_bias + hash_vaddr);
    unsigned int symbol_count = hash[1];
    if (symbol_count == 0 || sym_vaddr > max_vaddr ||
        symbol_count > (max_vaddr - sym_vaddr) / sizeof(elf32_sym_t) ||
        !image_range(sym_vaddr, symbol_count * sizeof(elf32_sym_t), min_vaddr, max_vaddr)) return -1;
    if (plt_rel_size && (plt_rel_type != ELF_DT_REL || !plt_rel_vaddr)) return -1;

    if (has_kernel_import) *has_kernel_import = 0;
    if (resolve_elf_relocations(load_bias, min_vaddr, max_vaddr, rel_vaddr, rel_size,
                                rel_entry_size, sym_vaddr, symbol_count,
                                str_vaddr, str_size, has_kernel_import) != 0 ||
        resolve_elf_relocations(load_bias, min_vaddr, max_vaddr, plt_rel_vaddr, plt_rel_size,
                                sizeof(elf32_rel_t), sym_vaddr, symbol_count,
                                str_vaddr, str_size, has_kernel_import) != 0) return -1;

    if (!image_range(eh->entry, 1, min_vaddr, max_vaddr)) return -1;
    *entry_out = load_bias + eh->entry;
    return 0;
}

/* Load a fully linked ET_EXEC image at the application's fixed address range. */
static int load_static_elf32(const unsigned char* file, unsigned int file_size,
                             unsigned char* load_area, unsigned int* entry_out) {
    const elf32_ehdr_t* eh = (const elf32_ehdr_t*)file;
    if (file_size < sizeof(*eh) || eh->ident[0] != 0x7F || eh->ident[1] != 'E' ||
        eh->ident[2] != 'L' || eh->ident[3] != 'F' || eh->ident[4] != 1 ||
        eh->ident[5] != 1 || eh->type != ELF_ET_EXEC || eh->machine != ELF_EM_386 ||
        eh->version != 1 || eh->ehsize != sizeof(*eh) ||
        eh->phentsize != sizeof(elf32_phdr_t) || eh->phnum == 0 || eh->phnum > 32 ||
        eh->phoff > file_size || eh->phnum > (file_size - eh->phoff) / eh->phentsize) return -1;

    const elf32_phdr_t* ph = (const elf32_phdr_t*)(file + eh->phoff);
    unsigned int min_vaddr = 0xFFFFFFFFu;
    unsigned int max_vaddr = 0;
    int has_load = 0;
    int has_dynamic = 0;
    int entry_executable = 0;
    for (unsigned int i = 0; i < eh->phnum; i++) {
        if (ph[i].type == ELF_PT_DYNAMIC) has_dynamic = 1;
        if (ph[i].type != ELF_PT_LOAD) continue;
        if (ph[i].filesz > ph[i].memsz || ph[i].offset > file_size ||
            ph[i].filesz > file_size - ph[i].offset ||
            ph[i].vaddr > 0xFFFFFFFFu - ph[i].memsz) return -1;
        if (!ph[i].memsz) continue;
        has_load = 1;
        if (ph[i].vaddr < min_vaddr) min_vaddr = ph[i].vaddr;
        if (ph[i].vaddr + ph[i].memsz > max_vaddr) max_vaddr = ph[i].vaddr + ph[i].memsz;
        if ((ph[i].flags & ELF_PF_X) && eh->entry >= ph[i].vaddr &&
            eh->entry < ph[i].vaddr + ph[i].memsz) entry_executable = 1;
    }
    if (!has_load || has_dynamic || !entry_executable || min_vaddr > USER_APP_BASE ||
        max_vaddr <= min_vaddr || max_vaddr - min_vaddr > USER_APP_MAX_SIZE) return -1;

    unsigned int load_base = (unsigned int)load_area;
    if (load_base < min_vaddr) return -1;
    unsigned int load_bias = load_base - min_vaddr;
    if (load_bias > 0xFFFFFFFFu - max_vaddr ||
        load_bias + max_vaddr > load_base + USER_APP_MAX_SIZE) return -1;

    if (zero_elf_bss(ph, eh->phnum, load_bias) != 0) return -1;
    if (copy_elf_load_segments(file, ph, eh->phnum, load_bias) != 0) return -1;
    *entry_out = load_bias + eh->entry;
    return 0;
}

typedef enum {
    USER_PROCESS_EMPTY = 0,
    USER_PROCESS_RUNNING,
    USER_PROCESS_EXITED,
    USER_PROCESS_REAPED
} user_process_state_t;

typedef struct {
    unsigned int pid;
    user_process_state_t state;
    int exit_code;
    void* stack;
    void* heap_arena;
    unsigned int heap_capacity;
    unsigned int heap_break;
} user_process_t;

static volatile int g_usercmd_active = 0;
static volatile int g_usercmd_app_started = 0;
static user_process_t g_user_process;
static unsigned int g_next_user_pid = 1;
static unsigned int g_usercmd_saved_esp = 0;
static unsigned int g_usercmd_saved_ebp = 0;
static unsigned int g_usercmd_saved_ebx = 0;
static unsigned int g_usercmd_saved_esi = 0;
static unsigned int g_usercmd_saved_edi = 0;
static volatile unsigned int g_usercmd_app_saved_esp = 0;
static volatile unsigned int g_usercmd_app_entry = 0;
static volatile unsigned int g_usercmd_app_argc = 0;
static volatile unsigned int g_usercmd_app_argv = 0;
static unsigned char g_usercmd_abort_stack[16384] __attribute__((aligned(16), used));
static volatile unsigned int g_usercmd_abort_vector;
static volatile unsigned int g_usercmd_abort_eip;
static volatile unsigned int g_usercmd_abort_error;
static volatile unsigned int g_usercmd_abort_cr2;
static registers_t* volatile g_usercmd_abort_regs;
static unsigned int g_usercmd_image_base;
static unsigned int g_usercmd_image_size;
static void usercmd_print_hex(unsigned int value);
static volatile unsigned int g_usercmd_user_entry;
static volatile unsigned int g_usercmd_user_esp;
static volatile unsigned int g_usercmd_user_exit_trampoline;
static volatile int g_usercmd_user_mode_active;
static volatile int g_usercmd_user_mapping_active;
static int usercmd_set_app_memory_access(int user);

static void usercmd_reset_keyboard_state(void) {
    keyboard_irq_reset_state();
    cyk_keyboard_driver_t* keyboard = modload_get_keyboard_driver();
    if (keyboard && keyboard->reset_state) keyboard->reset_state();
}

void usercmd_request_cancel(void) {
    if (g_usercmd_active && !g_usercmd_app_started)
        g_usercmd_cancel_requested = 1;
}

void usercmd_request_app_abort(void) {
    if (g_usercmd_active && g_usercmd_app_started && g_usercmd_user_mode_active)
        g_usercmd_abort_requested = 1;
}

void usercmd_apply_pending_app_abort(registers_t* regs) {
    if (!regs || (regs->cs & 3u) == 0 || !g_usercmd_abort_requested) return;
    g_usercmd_abort_requested = 0;
    (void)usercmd_redirect_user_irq(regs);
}

static int usercmd_cancel_loading(unsigned char** staged_image) {
    if (!g_usercmd_cancel_requested) return 0;
    g_usercmd_cancel_requested = 0;
    g_usercmd_abort_requested = 0;
    if (staged_image && *staged_image) {
        free(*staged_image);
        *staged_image = (unsigned char*)0;
    }
    g_usercmd_active = 0;
    g_usercmd_app_started = 0;
    g_usercmd_user_mode_active = 0;
    if (g_usercmd_user_mapping_active) {
        (void)usercmd_set_app_memory_access(0);
        g_usercmd_user_mapping_active = 0;
    }
    g_user_process.exit_code = 130;
    g_user_process.state = USER_PROCESS_EXITED;
    heap_set_allocation_owner(0);
    usercmd_reset_keyboard_state();
    print_string("^C\n");
    return 1;
}

static void usercmd_prepare_stack_guard(void) {
    volatile unsigned int* guard =
        (volatile unsigned int*)(USER_APP_STACK_BASE - USER_APP_STACK_GUARD_SIZE);
    for (unsigned int i = 0; i < USER_APP_STACK_GUARD_SIZE / sizeof(unsigned int); i++)
        guard[i] = USER_APP_STACK_GUARD_VALUE ^ i;
}

static int usercmd_stack_guard_intact(void) {
    volatile unsigned int* guard =
        (volatile unsigned int*)(USER_APP_STACK_BASE - USER_APP_STACK_GUARD_SIZE);
    for (unsigned int i = 0; i < USER_APP_STACK_GUARD_SIZE / sizeof(unsigned int); i++)
        if (guard[i] != (USER_APP_STACK_GUARD_VALUE ^ i)) return 0;
    return 1;
}

/* The standalone shell retains its kernel-mode stack. User apps receive a
 * separate stack and a page-isolated image/heap; legacy flat images and ELF
 * images with direct kernel imports still use the compatibility call path. */
static int usercmd_call_on_app_stack(int (*entry)(int, char**), int argc,
                                     char** argv, void* stack,
                                     unsigned int stack_size) {
    int result;
    g_usercmd_app_entry = (unsigned int)entry;
    g_usercmd_app_argc = (unsigned int)argc;
    g_usercmd_app_argv = (unsigned int)argv;
    unsigned int stack_top = (unsigned int)stack + stack_size;
    asm volatile(
        "movl %%esp, g_usercmd_app_saved_esp\n"
        "movl %1, %%esp\n"
        "andl $-16, %%esp\n"
        "subl $8, %%esp\n"
        "pushl g_usercmd_app_argv\n"
        "pushl g_usercmd_app_argc\n"
        "call *g_usercmd_app_entry\n"
        "movl g_usercmd_app_saved_esp, %%esp\n"
        : "=a"(result)
        : "m"(stack_top)
        : "ecx", "edx", "memory", "cc"
    );
    return result;
}

static int usercmd_copy_arguments_to_app_stack(int argc, char** argv,
                                                unsigned int stack_base,
                                                unsigned int stack_size,
                                                int* argc_out, char*** argv_out,
                                                unsigned int* stack_top_out) {
    unsigned int addresses[17];
    if (argc < 0 || argc > 16 || !argc_out || !argv_out || !stack_top_out)
        return -1;
    unsigned int cursor = (stack_base + stack_size) & ~15u;
    for (int i = argc - 1; i >= 0; i--) {
        if (!argv[i]) return -1;
        unsigned int length = (unsigned int)strlen(argv[i]) + 1u;
        if (length > cursor - stack_base) return -1;
        cursor -= length;
        memcpy((void*)cursor, argv[i], length);
        addresses[i] = cursor;
    }
    cursor &= ~3u;
    unsigned int vector_bytes = ((unsigned int)argc + 1u) * sizeof(unsigned int);
    if (vector_bytes > cursor - stack_base) return -1;
    cursor -= vector_bytes;
    unsigned int* user_argv = (unsigned int*)cursor;
    for (int i = 0; i < argc; i++) user_argv[i] = addresses[i];
    user_argv[argc] = 0;
    *argc_out = argc;
    *argv_out = (char**)user_argv;
    *stack_top_out = (cursor - 16u) & ~15u;
    return *stack_top_out >= stack_base ? 0 : -1;
}

static unsigned int usercmd_build_user_return_frame(int argc, char** argv,
                                                     unsigned int stack_top) {
    static const unsigned char trampoline[] = {
        0x89, 0xC3,                         /* mov ebx,eax */
        0xB8, 0x01, 0x00, 0x00, 0x00,     /* mov eax,SYS_EXIT */
        0xCD, 0x80,                         /* int 0x80 */
        0x0F, 0x0B                          /* ud2 if exit unexpectedly returns */
    };
    unsigned int trampoline_address = USER_APP_STACK_BASE + 16u;
    memcpy((void*)trampoline_address, trampoline, sizeof(trampoline));
    /* Match the i386 C ABI: ESP is 12 mod 16 at function entry. */
    unsigned int user_esp = stack_top - 20u;
    unsigned int* frame = (unsigned int*)user_esp;
    frame[0] = trampoline_address;
    frame[1] = (unsigned int)argc;
    frame[2] = (unsigned int)argv;
    g_usercmd_user_exit_trampoline = trampoline_address;
    return user_esp;
}

static __attribute__((naked, noreturn)) void usercmd_enter_user_mode(void) {
    __asm__ volatile(
        "movw $0x23, %ax\n\t"
        "movw %ax, %ds\n\t"
        "movw %ax, %es\n\t"
        "movw %ax, %fs\n\t"
        "movw %ax, %gs\n\t"
        "pushl $0x23\n\t"
        "pushl g_usercmd_user_esp\n\t"
        "pushl $0x202\n\t"
        "pushl $0x1B\n\t"
        "pushl g_usercmd_user_entry\n\t"
        "iret\n"
    );
}

int usercmd_redirect_user_irq(registers_t* regs) {
    if (!regs || !g_usercmd_user_mode_active || !g_usercmd_user_exit_trampoline)
        return 0;
    regs->eip = g_usercmd_user_exit_trampoline;
    regs->eax = 130;
    regs->eflags &= ~(1u << 9);
    return 1;
}

static int usercmd_set_app_memory_access(int user) {
    int result = 0;
    if (g_usercmd_image_size && paging_set_user_range(
            g_usercmd_image_base, g_usercmd_image_size, user) != 0) result = -1;
    if (paging_set_user_range(USER_APP_STACK_BASE, USER_APP_STACK_SIZE, user) != 0)
        result = -1;
    if (g_user_process.heap_capacity && paging_set_user_range(
            USER_APP_HEAP_BASE, g_user_process.heap_capacity, user) != 0)
        result = -1;
    return result;
}

// Internal helper to convert character to uppercase
static char uc(char c) {
    if (c >= 'a' && c <= 'z') return c - ('a' - 'A');
    return c;
}

// Case-insensitive null-terminated string comparison
static int str_casecmp(const char* a, const char* b) {
    while (*a && *b) {
        char ca = uc(*a);
        char cb = uc(*b);
        if (ca != cb) return (unsigned char)ca - (unsigned char)cb;
        a++; b++;
    }
    return (unsigned char)uc(*a) - (unsigned char)uc(*b);
}

static int build_path(const char* directory, const char* filename, char* output, int capacity) {
    int index = 0;
    while (directory[index] && index < capacity - 1) {
        output[index] = directory[index];
        index++;
    }
    int source_index = 0;
    while (filename[source_index] && index < capacity - 1) {
        output[index++] = filename[source_index++];
    }
    output[index] = '\0';
    return filename[source_index] == '\0' ? 0 : -1;
}

// Skip whitespace and comment lines, extract next token into out_token
static const char* next_token(const char* p, char* out, int max) {
    // Skip whitespace
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;

    if (*p == '\0') return (void*)0;

    // Skip comment lines
    if (*p == '#') {
        while (*p != '\0' && *p != '\n') p++;
        return next_token(p, out, max);
    }

    int i = 0;
    while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && i < max - 1) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return p;
}

// Read the external command list and search for an alias matching cmd_name.
// If found, writes the binary filename into out_bin and returns 1.
static int find_alias(const char* cmd_name, char* out_bin, int out_max) {
    vfs_file_t cfg;
    if (vfs_find_file(USERCMDS_FILE, &cfg) != 0) {
        if (vfs_find_file(USERCMDS_FALLBACK, &cfg) != 0 &&
            vfs_find_file(USERCMDS_LEGACY, &cfg) != 0 &&
            vfs_find_file(USERCMDS_OLD_FALLBACK, &cfg) != 0) {
            return 0;
        }
    }

    char buf[1024];
    memset(buf, 0, sizeof(buf));
    int bytes = vfs_read_at(&cfg, 0, buf, sizeof(buf) - 1);
    if (bytes <= 0) return 0;

    const char* cursor = buf;
    char alias[32];
    char binary[USER_PATH_MAX];

    while ((cursor = next_token(cursor, alias, sizeof(alias))) != (void*)0) {
        cursor = next_token(cursor, binary, sizeof(binary));
        if (!cursor) break;

        if (str_casecmp(alias, cmd_name) == 0) {
            // Copy matched binary filename to output
            int i = 0;
            while (binary[i] != '\0' && i < out_max - 1) {
                out_bin[i] = binary[i];
                i++;
            }
            out_bin[i] = '\0';
            return 1;
        }
    }
    return 0;
}

static void shell_output_string(const char* text) {
    if (syscall_pipe_output_active()) {
        syscall_pipe_write(text, strlen(text));
    } else {
        print_string(text);
    }
}

static void shell_output_char(char ch) {
    if (syscall_pipe_output_active()) {
        syscall_pipe_write(&ch, 1);
    } else {
        print_char(ch);
    }
}

int exec_user_command(const char* cmd_name, const char* args) {
    if (!cmd_name || cmd_name[0] == '\0') return 0;
    g_usercmd_app_started = 0;
    g_usercmd_cancel_requested = 0;
    g_usercmd_user_mode_active = 0;
    g_usercmd_user_mapping_active = 0;
    g_user_process.exit_code = 0;
    volatile int app_ret = 0;
    unsigned char* staged_image = (unsigned char*)0;
    void* dest = (void*)USER_APP_BASE;
    unsigned int entry_address = USER_APP_BASE;
    int use_user_mode = 0;

    // Cover command lookup and disk loading too. Ctrl+C abandons this function
    // through usercmd_return_label, so capture its stable frame before work.
    asm volatile(
        "pushfl\n"
        "cli\n"
        /* Account for pushfl: resume with ESP restored by the following popfl. */
        "leal 4(%%esp), %%eax\n"
        "movl %%eax, %0\n"
        "movl %%ebp, %1\n"
        "movl %%ebx, %2\n"
        "movl %%esi, %3\n"
        "movl %%edi, %4\n"
        "movl $1, %5\n"
        "popfl\n"
        : "=m"(g_usercmd_saved_esp), "=m"(g_usercmd_saved_ebp),
          "=m"(g_usercmd_saved_ebx), "=m"(g_usercmd_saved_esi),
          "=m"(g_usercmd_saved_edi), "=m"(g_usercmd_active)
        :
        : "eax", "memory"
    );

    char target_file[USER_PATH_MAX];
    target_file[0] = '\0';

    // Look up alias in USERCMDS.TXT
    if (!find_alias(cmd_name, target_file, sizeof(target_file))) {
        // Fallback: check if cmd_name is a direct file or has .BIN / .COM extension
        vfs_file_t probe;
        if (vfs_find_file(cmd_name, &probe) == 0) {
            int i = 0;
            while (cmd_name[i] && i < (int)sizeof(target_file) - 1) {
                target_file[i] = cmd_name[i];
                i++;
            }
            target_file[i] = '\0';
        } else {
            char name_buf[16];
            int i = 0;
            while (cmd_name[i] && cmd_name[i] != '.' && i < 8) {
                name_buf[i] = cmd_name[i];
                i++;
            }
            name_buf[i] = '\0';

            // Try <cmd>.BIN
            char test_bin[16];
            int j = 0;
            while (name_buf[j]) { test_bin[j] = name_buf[j]; j++; }
            test_bin[j++] = '.';
            test_bin[j++] = 'B';
            test_bin[j++] = 'I';
            test_bin[j++] = 'N';
            test_bin[j] = '\0';

            char app_target[USER_PATH_MAX];
            char legacy_target[USER_PATH_MAX];
            build_path(USER_BIN_DIR, test_bin, app_target, sizeof(app_target));
            build_path(LEGACY_BIN_DIR, test_bin, legacy_target, sizeof(legacy_target));

            if (vfs_find_file(test_bin, &probe) == 0) {
                for (int k = 0; k <= j; k++) target_file[k] = test_bin[k];
            } else if (vfs_find_file(app_target, &probe) == 0) {
                memcpy(target_file, app_target, strlen(app_target) + 1);
            } else if (vfs_find_file(legacy_target, &probe) == 0) {
                memcpy(target_file, legacy_target, strlen(legacy_target) + 1);
            } else {
                // Try <cmd>.COM
                test_bin[j - 3] = 'C';
                test_bin[j - 2] = 'O';
                test_bin[j - 1] = 'M';
                if (vfs_find_file(test_bin, &probe) == 0) {
                    for (int k = 0; k <= j; k++) target_file[k] = test_bin[k];
                } else {
                    build_path(USER_BIN_DIR, test_bin, app_target, sizeof(app_target));
                    build_path(LEGACY_BIN_DIR, test_bin, legacy_target, sizeof(legacy_target));
                    if (vfs_find_file(app_target, &probe) == 0) {
                        memcpy(target_file, app_target, strlen(app_target) + 1);
                    } else if (vfs_find_file(legacy_target, &probe) == 0) {
                        memcpy(target_file, legacy_target, strlen(legacy_target) + 1);
                    } else {
                        g_usercmd_active = 0;
                        return 0;
                    }
                }
            }
        }
    }

    // Locate the binary through the mounted filesystem.
    vfs_file_t app_entry;
    if (vfs_find_file(target_file, &app_entry) != 0) {
        // If not found in current directory and not an absolute path, check root directory
        int found_in_root = 0;
        if (target_file[0] != '/' && target_file[0] != '\\') {
            char root_target[USER_PATH_MAX];
            root_target[0] = '/';
            int t = 0;
            while (target_file[t] && t < (int)sizeof(root_target) - 2) {
                root_target[t + 1] = target_file[t];
                t++;
            }
            root_target[t + 1] = '\0';
            if (vfs_find_file(root_target, &app_entry) == 0) {
                found_in_root = 1;
            }
        }
        if (!found_in_root) {
            g_usercmd_active = 0;
            print_color_string("CykShell: executable not found: ", VGA_RED);
            print_string(target_file);
            print_string("\n");
            return 1; // Handled (error shown)
        }
    }

    // Check if executable file is empty
    if (app_entry.size == 0) {
        g_usercmd_active = 0;
        print_color_string("CykShell: executable is empty (0 bytes): ", VGA_RED);
        print_string(target_file);
        print_string("\n");
        return 1;
    }
    if (app_entry.size > USER_APP_FILE_MAX) {
        g_usercmd_active = 0;
        print_color_string("CykShell: executable file is too large: ", VGA_RED);
        print_string(target_file);
        print_string("\n");
        return 1;
    }

    staged_image = (unsigned char*)malloc(app_entry.size);
    if (!staged_image) {
        g_usercmd_active = 0;
        print_color_string("CykShell: not enough memory to load: ", VGA_RED);
        print_string(target_file);
        print_string("\n");
        return 1;
    }
    int bytes_read = vfs_read_at(&app_entry, 0, staged_image, app_entry.size);
    if (bytes_read != (int)app_entry.size) {
        free(staged_image);
        staged_image = (unsigned char*)0;
        g_usercmd_active = 0;
        print_color_string("CykShell: failed to load: ", VGA_RED);
        print_string(target_file);
        print_string("\n");
        return 1;
    }
    if (usercmd_cancel_loading(&staged_image)) return 1;

    if (app_entry.size >= 4 && staged_image[0] == 0x7F && staged_image[1] == 'E' &&
        staged_image[2] == 'L' && staged_image[3] == 'F') {
        unsigned short elf_type = app_entry.size >= sizeof(elf32_ehdr_t)
            ? ((const elf32_ehdr_t*)staged_image)->type : 0;
        if (elf_type == ELF_ET_DYN) {
            dest = (void*)USER_DYNAMIC_APP_BASE;
        }
        int has_kernel_import = 0;
        int load_result = elf_type == ELF_ET_DYN
            ? load_dynamic_elf32(staged_image, app_entry.size, (unsigned char*)dest,
                                 &entry_address, &has_kernel_import)
            : elf_type == ELF_ET_EXEC
                ? load_static_elf32(staged_image, app_entry.size, (unsigned char*)dest, &entry_address)
                : -1;
        if (usercmd_cancel_loading(&staged_image)) return 1;
        if (load_result != 0) {
            free(staged_image);
            staged_image = (unsigned char*)0;
            g_usercmd_active = 0;
            print_color_string("CykShell: invalid ELF app or unresolved kernel import: ", VGA_RED);
            print_string(target_file);
            print_string("\n");
            return 1;
        }
        use_user_mode = elf_type == ELF_ET_EXEC ||
                        (elf_type == ELF_ET_DYN && !has_kernel_import);
        g_usercmd_image_base = (unsigned int)dest;
        g_usercmd_image_size = elf_type == ELF_ET_DYN
            ? USER_DYNAMIC_APP_MAX_SIZE : USER_APP_MAX_SIZE;
    } else {
        if (app_entry.size > USER_APP_MAX_SIZE) {
            free(staged_image);
            staged_image = (unsigned char*)0;
            g_usercmd_active = 0;
            print_color_string("CykShell: flat app exceeds its memory limit: ", VGA_RED);
            print_string(target_file);
            print_string("\n");
            return 1;
        }
        memset(dest, 0, USER_APP_MAX_SIZE);
        memcpy(dest, staged_image, app_entry.size);
        g_usercmd_image_base = USER_APP_BASE;
        g_usercmd_image_size = USER_APP_MAX_SIZE;
    }
    free(staged_image);
    staged_image = (unsigned char*)0;

    // Tokenize arguments into standard (int argc, char** argv)
    #define MAX_APP_ARGS 16
    static char s_arg_buf[256];
    static char* s_argv[MAX_APP_ARGS + 1];
    int argc = 0;

    // argv[0] is the command name as invoked
    s_argv[argc++] = (char*)cmd_name;

    if (args && args[0] != '\0') {
        int i = 0;
        while (args[i] != '\0' && i < (int)sizeof(s_arg_buf) - 1) {
            s_arg_buf[i] = args[i];
            i++;
        }
        s_arg_buf[i] = '\0';

        char* p = s_arg_buf;
        while (*p != '\0' && argc < MAX_APP_ARGS) {
            while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
                *p = '\0';
                p++;
            }
            if (*p == '\0') break;

            if (*p == '"' || *p == '\'') {
                char quote = *p++;
                s_argv[argc++] = p;
                while (*p != '\0' && *p != quote) p++;
                if (*p != '\0') {
                    *p = '\0';
                    p++;
                }
            } else {
                s_argv[argc++] = p;
                while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
                    p++;
                }
                if (*p != '\0') {
                    *p = '\0';
                    p++;
                }
            }
        }
    }
    s_argv[argc] = (void*)0; // NULL termination

    int app_argc = 0;
    char** app_argv = (char**)0;
    unsigned int app_stack_top = 0;
    if (usercmd_copy_arguments_to_app_stack(argc, s_argv, USER_APP_STACK_BASE,
            USER_APP_STACK_SIZE, &app_argc, &app_argv, &app_stack_top) != 0) {
        g_usercmd_active = 0;
        print_color_string("CykShell: application arguments exceed its stack.\n", VGA_RED);
        return 1;
    }

    // Execute as standard 32-bit function: int main(int argc, char** argv)
    typedef int (*app_main_fn)(int, char**);
    void* app_stack = (void*)USER_APP_STACK_BASE;
    g_user_process.pid = g_next_user_pid++;
    if (g_next_user_pid == 0) g_next_user_pid = 1;
    g_user_process.state = USER_PROCESS_RUNNING;
    g_user_process.exit_code = 0;
    g_user_process.stack = app_stack;
    g_user_process.heap_arena = (void*)USER_APP_HEAP_BASE;
    unsigned int available = heap_get_usable_memory_end() > USER_APP_HEAP_BASE
        ? heap_get_usable_memory_end() - USER_APP_HEAP_BASE : 0;
    available &= ~0xFFFu;
    g_user_process.heap_capacity = available < USER_APP_HEAP_MAX_SIZE
        ? available : USER_APP_HEAP_MAX_SIZE;
    g_user_process.heap_break = 0;
    heap_set_allocation_owner(g_user_process.pid);

    if (use_user_mode && usercmd_set_app_memory_access(1) != 0) {
        (void)usercmd_set_app_memory_access(0);
        g_usercmd_app_started = 0;
        g_usercmd_active = 0;
        heap_set_allocation_owner(0);
        print_color_string("CykShell: failed to prepare protected app memory.\n", VGA_RED);
        return 1;
    }

    g_usercmd_user_mapping_active = use_user_mode;

    app_main_fn entry = (app_main_fn)entry_address;
    usercmd_prepare_stack_guard();
    if (g_usercmd_cancel_requested && usercmd_cancel_loading(&staged_image)) return 1;
    if (use_user_mode) {
        g_usercmd_user_entry = entry_address;
        g_usercmd_user_esp = usercmd_build_user_return_frame(
            app_argc, app_argv, app_stack_top);
        g_usercmd_user_mode_active = 1;
        asm volatile("cli" ::: "memory");
        if (g_usercmd_cancel_requested) {
            asm volatile("sti" ::: "memory");
            if (usercmd_cancel_loading(&staged_image)) return 1;
        }
        g_usercmd_app_started = 1;
        usercmd_enter_user_mode();
    } else {
        if (usercmd_cancel_loading(&staged_image)) return 1;
        g_usercmd_app_started = 1;
        app_ret = usercmd_call_on_app_stack(entry, app_argc, app_argv, app_stack,
                                            app_stack_top - USER_APP_STACK_BASE);
    }
    if (!usercmd_stack_guard_intact()) {
        print_color_string("Application stack overflow detected; process stopped.\n", VGA_RED);
        app_ret = -11;
    }
    if (g_user_process.state == USER_PROCESS_RUNNING) {
        g_user_process.exit_code = app_ret;
        g_user_process.state = USER_PROCESS_EXITED;
    }

    asm volatile("usercmd_return_label:\n");
    if (g_usercmd_user_mapping_active) {
        (void)usercmd_set_app_memory_access(0);
        g_usercmd_user_mapping_active = 0;
    }
    usercmd_reset_keyboard_state();
    g_usercmd_user_mode_active = 0;
    g_usercmd_active = 0;
    g_usercmd_app_started = 0;
    heap_set_allocation_owner(0);
    syscall_close_process_fds(g_user_process.pid);
    heap_free_allocations_by_owner(g_user_process.pid);
    g_user_process.stack = (void*)0;
    g_user_process.heap_arena = (void*)0;
    g_user_process.heap_capacity = 0;
    g_user_process.heap_break = 0;
    if (staged_image) {
        free(staged_image);
        staged_image = (unsigned char*)0;
    }
    cyk_display_driver_t* dpy = modload_get_display_driver();
    if (dpy && dpy->get_mode && dpy->get_mode() != 0) dpy->set_mode(0);
    syscall_flush_deferred_console();
    (void)app_ret;

    return 1;
}

void usercmd_terminate(int exit_code) {
    if (!g_usercmd_active || !g_usercmd_app_started) return;
    g_usercmd_active = 0;
    g_usercmd_app_started = 0;
    g_usercmd_user_mode_active = 0;
    g_user_process.exit_code = exit_code;
    g_user_process.state = USER_PROCESS_EXITED;

    // If a graphics mode was left active, reset back to text mode (Mode 03h)
    cyk_display_driver_t* dpy = modload_get_display_driver();
    if (dpy && dpy->get_mode && dpy->get_mode() != 0) {
        dpy->set_mode(0);
    }
    syscall_flush_deferred_console();
    usercmd_reset_keyboard_state();

    asm volatile(
        "movl %0, %%esp\n"
        "movl %1, %%ebp\n"
        "movl %2, %%ebx\n"
        "movl %3, %%esi\n"
        "movl %4, %%edi\n"
        "sti\n"
        "jmp usercmd_return_label\n"
        :
        : "m"(g_usercmd_saved_esp), "m"(g_usercmd_saved_ebp),
          "m"(g_usercmd_saved_ebx), "m"(g_usercmd_saved_esi),
          "m"(g_usercmd_saved_edi)
        : "memory"
    );
    __builtin_unreachable();
}

static __attribute__((noreturn, used)) void usercmd_abort_from_irq_impl(void) {
    if (!g_usercmd_active) {
        for (;;) asm volatile("hlt");
    }
    g_usercmd_active = 0;
    g_usercmd_user_mode_active = 0;
    if (g_usercmd_app_started) {
        g_user_process.exit_code = 130;
        g_user_process.state = USER_PROCESS_EXITED;
        g_usercmd_app_started = 0;
    }
    cyk_display_driver_t* dpy = modload_get_display_driver();
    if (dpy && dpy->get_mode && dpy->get_mode() != 0) {
        dpy->set_mode(0);
    }
    syscall_flush_deferred_console();
    usercmd_reset_keyboard_state();
    print_string("^C\n");
    asm volatile(
        "movl %0, %%esp\n"
        "movl %1, %%ebp\n"
        "movl %2, %%ebx\n"
        "movl %3, %%esi\n"
        "movl %4, %%edi\n"
        "sti\n"
        "jmp usercmd_return_label\n"
        :
        : "m"(g_usercmd_saved_esp), "m"(g_usercmd_saved_ebp),
          "m"(g_usercmd_saved_ebx), "m"(g_usercmd_saved_esi),
          "m"(g_usercmd_saved_edi)
        : "memory"
    );
    __builtin_unreachable();
}

/* IRQ redirection enters without a C call frame. Move off the interrupted
 * application stack before calling code that touches kernel subsystems. */
__attribute__((naked, noreturn)) void usercmd_abort_from_irq(void) {
    __asm__ volatile(
        "movl $g_usercmd_abort_stack+16384, %esp\n"
        "andl $-16, %esp\n"
        "call usercmd_abort_from_irq_impl\n"
        "ud2\n"
    );
}

static void usercmd_print_hex(unsigned int value) {
    static const char digits[] = "0123456789ABCDEF";
    char output[11] = "0x00000000";
    for (unsigned int i = 0; i < 8; i++)
        output[2 + i] = digits[(value >> ((7 - i) * 4)) & 0xF];
    print_string(output);
}

static __attribute__((noreturn, used)) void usercmd_abort_from_exception_impl(void) {
    unsigned int vector = g_usercmd_abort_vector;
    unsigned int eip = g_usercmd_abort_eip;
    unsigned int error_code = g_usercmd_abort_error;
    if (!g_usercmd_active) {
        for (;;) asm volatile("cli; hlt");
    }
    g_usercmd_active = 0;
    g_usercmd_user_mode_active = 0;
    if (g_usercmd_app_started) {
        g_user_process.exit_code = 128 + (int)vector;
        g_user_process.state = USER_PROCESS_EXITED;
        g_usercmd_app_started = 0;
    }
    cyk_display_driver_t* dpy = modload_get_display_driver();
    if (dpy && dpy->get_mode && dpy->get_mode() != 0) dpy->set_mode(0);
    syscall_flush_deferred_console();
    usercmd_reset_keyboard_state();
    print_color_string("Application stopped after CPU exception: vector ", VGA_RED);
    usercmd_print_hex(vector);
    print_string(", EIP ");
    usercmd_print_hex(eip);
    print_string(", error ");
    usercmd_print_hex(error_code);
    if (vector == 14) {
        print_string(", CR2 ");
        usercmd_print_hex(g_usercmd_abort_cr2);
        if (g_usercmd_abort_regs) {
            print_string(", EAX "); usercmd_print_hex(g_usercmd_abort_regs->eax);
            print_string(", EBX "); usercmd_print_hex(g_usercmd_abort_regs->ebx);
            print_string(", ECX "); usercmd_print_hex(g_usercmd_abort_regs->ecx);
            print_string(", EDX "); usercmd_print_hex(g_usercmd_abort_regs->edx);
            print_string(", ESI "); usercmd_print_hex(g_usercmd_abort_regs->esi);
            print_string(", EDI "); usercmd_print_hex(g_usercmd_abort_regs->edi);
            print_string(", EBP "); usercmd_print_hex(g_usercmd_abort_regs->ebp);
            print_string(", ESP ");
            usercmd_print_hex((g_usercmd_abort_regs->cs & 3u)
                ? g_usercmd_abort_regs->useresp : g_usercmd_abort_regs->esp_dummy);
            if ((g_usercmd_abort_regs->cs & 3u) != 0 &&
                paging_user_range_valid(g_usercmd_abort_regs->useresp,
                                        8u * sizeof(unsigned int))) {
                volatile const unsigned int* stack =
                    (volatile const unsigned int*)g_usercmd_abort_regs->useresp;
                print_string(", STACK");
                for (unsigned int i = 0; i < 8; i++) {
                    print_char(' ');
                    usercmd_print_hex(stack[i]);
                }
            }
        }
    }
    print_string("; returned to shell.\n");
    asm volatile(
        "movl %0, %%esp\n"
        "movl %1, %%ebp\n"
        "movl %2, %%ebx\n"
        "movl %3, %%esi\n"
        "movl %4, %%edi\n"
        "sti\n"
        "jmp usercmd_return_label\n"
        :
        : "m"(g_usercmd_saved_esp), "m"(g_usercmd_saved_ebp),
          "m"(g_usercmd_saved_ebx), "m"(g_usercmd_saved_esi),
          "m"(g_usercmd_saved_edi)
        : "memory"
    );
    __builtin_unreachable();
}

/* Preserve exception details before moving to the kernel-owned abort stack.
 * From CPL3, the CPU first switches to the TSS interrupt stack. */
__attribute__((naked, noreturn)) void usercmd_abort_from_exception(
    unsigned int, unsigned int, unsigned int, registers_t*) {
    __asm__ volatile(
        "movl 4(%esp), %eax\n"
        "movl %eax, g_usercmd_abort_vector\n"
        "movl 8(%esp), %eax\n"
        "movl %eax, g_usercmd_abort_eip\n"
        "movl 12(%esp), %eax\n"
        "movl %eax, g_usercmd_abort_error\n"
        "movl %cr2, %eax\n"
        "movl %eax, g_usercmd_abort_cr2\n"
        "movl 16(%esp), %eax\n"
        "movl %eax, g_usercmd_abort_regs\n"
        "movl $g_usercmd_abort_stack+16384, %esp\n"
        "andl $-16, %esp\n"
        "call usercmd_abort_from_exception_impl\n"
        "ud2\n"
    );
}

int usercmd_is_running(void) {
    return g_usercmd_active;
}

int usercmd_is_app_started(void) {
    return g_usercmd_app_started;
}

unsigned int usercmd_current_pid(void) {
    return g_usercmd_app_started ? g_user_process.pid : 0;
}

static int usercmd_ensure_app_heap(unsigned int minimum_capacity) {
    if (g_user_process.heap_arena) {
        return minimum_capacity <= g_user_process.heap_capacity ? 0 : -1;
    }
    unsigned int available = heap_get_usable_memory_end() > USER_APP_HEAP_BASE
        ? heap_get_usable_memory_end() - USER_APP_HEAP_BASE : 0;
    available &= ~0xFFFu;
    if (available > USER_APP_HEAP_MAX_SIZE) available = USER_APP_HEAP_MAX_SIZE;
    if (minimum_capacity > available) return -1;
    g_user_process.heap_arena = (void*)USER_APP_HEAP_BASE;
    g_user_process.heap_capacity = available;
    g_user_process.heap_break = 0;
    return 0;
}

void* usercmd_app_sbrk(int increment) {
    if (!g_usercmd_app_started) return sbrk(increment);
    unsigned int minimum = increment > 0 ? (unsigned int)increment : 1;
    if (usercmd_ensure_app_heap(minimum) != 0) return (void*)-1;

    unsigned int previous = g_user_process.heap_break;
    if (previous > g_user_process.heap_capacity) return (void*)-1;
    if (increment > 0) {
        if ((unsigned int)increment > g_user_process.heap_capacity - previous)
            return (void*)-1;
        g_user_process.heap_break += (unsigned int)increment;
    } else if (increment < 0) {
        unsigned int amount = 0U - (unsigned int)increment;
        if (amount > previous) return (void*)-1;
        g_user_process.heap_break -= amount;
    }
    return (unsigned char*)g_user_process.heap_arena + previous;
}

unsigned int usercmd_app_brk(unsigned int requested) {
    if (!g_usercmd_app_started) return (unsigned int)sbrk(0);
    if (usercmd_ensure_app_heap(1) != 0) return (unsigned int)-1;
    unsigned int base = (unsigned int)g_user_process.heap_arena;
    if (requested >= base && requested - base <= g_user_process.heap_capacity)
        g_user_process.heap_break = requested - base;
    return base + g_user_process.heap_break;
}

int usercmd_wait(int* exit_code) {
    if (g_user_process.state != USER_PROCESS_EXITED) return -1;
    int pid = (int)g_user_process.pid;
    if (exit_code) *exit_code = g_user_process.exit_code;
    g_user_process.state = USER_PROCESS_REAPED;
    return pid;
}

int exec_shell_binary(void) {
    vfs_file_t shell_entry;
    const char* shell_path = "/BIN/SHELL.BIN";
    if (vfs_find_file(shell_path, &shell_entry) != 0) {
        shell_path = "/CYKOS/SHELL.BIN";
        if (vfs_find_file(shell_path, &shell_entry) != 0) {
            print_color_string("CykShell: standalone shell image not found.\n", VGA_RED);
            return 0;
        }
    }
    unsigned int shell_base_address = (unsigned int)__shell_image_base;
    unsigned int shell_image_capacity = (unsigned int)__shell_image_limit - shell_base_address;
    if (shell_entry.size == 0 || shell_entry.size > shell_image_capacity) {
        print_color_string("CykShell: invalid shell image size\n", VGA_RED);
        return -1;
    }

    void* shell_base = (void*)shell_base_address;
    memset(shell_base, 0, shell_image_capacity);
    int bytes_read = vfs_read_at(&shell_entry, 0, shell_base, shell_image_capacity);
    if (bytes_read <= 0) {
        print_color_string("CykShell: failed to load standalone shell image\n", VGA_RED);
        return -1;
    }

    unsigned char* image = (unsigned char*)shell_base;
    if (image[0] == 0x7F && image[1] == 'E' && image[2] == 'L' && image[3] == 'F') {
        print_color_string("CykShell: shell image must be a flat binary\n", VGA_RED);
        return -1;
    }

    typedef int (*shell_main_fn)(int, char**);
    // Keep the standalone shell away from the kernel's small boot stack and
    // separate from the foreground app stack used by nested commands.
    usercmd_call_on_app_stack((shell_main_fn)shell_base, 0, (char**)0,
                              (void*)USER_SHELL_STACK_BASE, USER_SHELL_STACK_SIZE);
    return 1;
}

void list_user_commands(void) {
    vfs_file_t cfg;
    const char* filename = USERCMDS_FILE;
    if (vfs_find_file(USERCMDS_FILE, &cfg) != 0) {
        if (vfs_find_file(USERCMDS_FALLBACK, &cfg) != 0) {
            filename = USERCMDS_LEGACY;
            if (vfs_find_file(filename, &cfg) != 0) {
                filename = USERCMDS_OLD_FALLBACK;
                if (vfs_find_file(filename, &cfg) != 0) return;
            }
        } else {
            filename = USERCMDS_FALLBACK;
        }
    }

    char buf[1024];
    memset(buf, 0, sizeof(buf));
    int bytes = vfs_read_at(&cfg, 0, buf, sizeof(buf) - 1);
    if (bytes <= 0) return;

    shell_output_string("\nExternal Commands (");
    shell_output_string(filename);
    shell_output_string("):\n");

    const char* cursor = buf;
    char alias[32];
    char binary[USER_PATH_MAX];

    while ((cursor = next_token(cursor, alias, sizeof(alias))) != (void*)0) {
        cursor = next_token(cursor, binary, sizeof(binary));
        if (!cursor) break;

        shell_output_string("  ");
        shell_output_string(alias);
        int pad = (int)strlen(alias);
        while (pad < 12) {
            shell_output_char(' ');
            pad++;
        }
        shell_output_string("- ");
        shell_output_string(binary);
        shell_output_string("\n");
    }
}
