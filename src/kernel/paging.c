#include "paging.h"
#include "heap.h"

#define PAGE_SIZE 4096u
#define PAGE_MASK 0xFFFFF000u
#define PAGE_PRESENT 0x001u
#define PAGE_WRITE   0x002u
#define PAGE_USER    0x004u
#define PAGE_LARGE   0x080u
#define PAGE_PWT     0x008u
#define PAGE_PCD     0x010u
#define LARGE_PAGE_SIZE 0x400000u
#define MANAGED_LIMIT 0x02000000u

/* Keep the low 32 MiB in 4 KiB pages so app mappings can later be isolated. */
static unsigned int* g_page_directory;
static unsigned int* g_low_page_tables;
static void* g_page_structure_allocation;
static int g_paging_ready;

static int paging_cpu_has_pse(void) {
    unsigned int original_flags, toggled_flags;
    __asm__ volatile(
        "pushfl\n\t"
        "popl %0\n\t"
        "movl %0, %1\n\t"
        "xorl $0x00200000, %1\n\t"
        "pushl %1\n\t"
        "popfl\n\t"
        "pushfl\n\t"
        "popl %1\n\t"
        "pushl %0\n\t"
        "popfl"
        : "=&r"(original_flags), "=&r"(toggled_flags)
        :
        : "cc", "memory");
    if (((original_flags ^ toggled_flags) & 0x00200000u) == 0) return 0;

    unsigned int eax = 1, ebx, ecx, edx;
    __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    return (edx & (1u << 3)) != 0;
}

static void paging_flush_tlb(void) {
    unsigned int cr3;
    __asm__ volatile("movl %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("movl %0, %%cr3" : : "r"(cr3) : "memory");
}

int paging_init(void) {
    if (g_paging_ready) return 0;

    /* The heap is initialized before paging; keep the 36 KiB tables out of
       the already crowded kernel BSS and retain their allocation forever. */
    g_page_structure_allocation = malloc(9u * PAGE_SIZE + PAGE_SIZE - 1u);
    if (!g_page_structure_allocation) return -1;
    unsigned int aligned = ((unsigned int)g_page_structure_allocation + PAGE_SIZE - 1u) & PAGE_MASK;
    g_page_directory = (unsigned int*)aligned;
    g_low_page_tables = (unsigned int*)(aligned + PAGE_SIZE);

    int has_pse = paging_cpu_has_pse();
    for (unsigned int directory = 0; directory < 1024; directory++) {
        if (has_pse) {
            unsigned int base = directory * LARGE_PAGE_SIZE;
            g_page_directory[directory] = base | PAGE_PRESENT | PAGE_WRITE | PAGE_LARGE;
        } else {
            g_page_directory[directory] = 0;
        }
    }

    for (unsigned int table = 0; table < 8; table++) {
        for (unsigned int page = 0; page < 1024; page++) {
            unsigned int address = table * LARGE_PAGE_SIZE + page * PAGE_SIZE;
            g_low_page_tables[table * 1024u + page] = address | PAGE_PRESENT | PAGE_WRITE;
        }
        g_page_directory[table] = (unsigned int)(g_low_page_tables + table * 1024u) |
                                  PAGE_PRESENT | PAGE_WRITE;
    }

    unsigned int directory_address = (unsigned int)g_page_directory;
    __asm__ volatile("movl %0, %%cr3" : : "r"(directory_address) : "memory");

    if (has_pse) {
        unsigned int cr4;
        __asm__ volatile("movl %%cr4, %0" : "=r"(cr4));
        cr4 |= 1u << 4; /* PSE for the rest of the identity map. */
        __asm__ volatile("movl %0, %%cr4" : : "r"(cr4) : "memory");
    }

    unsigned int cr0;
    __asm__ volatile("movl %%cr0, %0" : "=r"(cr0));
    cr0 |= (1u << 31) | (1u << 16); /* PG and supervisor write protection. */
    __asm__ volatile("movl %0, %%cr0" : : "r"(cr0) : "memory");
    g_paging_ready = 1;
    return 0;
}

int paging_map_identity_range(unsigned int address, unsigned int length) {
    if (!g_paging_ready || !length || address > 0xFFFFFFFFu - length) return -1;
    unsigned int first_directory = address / LARGE_PAGE_SIZE;
    unsigned int last_directory = (address + length - 1u) / LARGE_PAGE_SIZE;
    for (unsigned int directory = first_directory; directory <= last_directory; directory++) {
        unsigned int pde = g_page_directory[directory];
        if (pde & PAGE_PRESENT) continue;
        unsigned int* table = (unsigned int*)malloc(PAGE_SIZE);
        if (!table) return -1;
        unsigned int base = directory * LARGE_PAGE_SIZE;
        for (unsigned int page = 0; page < 1024; page++)
            table[page] = (base + page * PAGE_SIZE) | PAGE_PRESENT | PAGE_WRITE;
        g_page_directory[directory] = (unsigned int)table | PAGE_PRESENT | PAGE_WRITE;
    }
    paging_flush_tlb();
    return 0;
}

/* Split large identity pages where needed and map device registers uncached. */
int paging_map_mmio_range(unsigned int address, unsigned int length) {
    if (!g_paging_ready || !length || address > 0xFFFFFFFFu - length) return -1;
    unsigned int first = address & PAGE_MASK;
    unsigned int last = (address + length - 1u) & PAGE_MASK;
    for (unsigned int page_address = first;; page_address += PAGE_SIZE) {
        unsigned int directory = page_address / LARGE_PAGE_SIZE;
        unsigned int page = (page_address % LARGE_PAGE_SIZE) / PAGE_SIZE;
        unsigned int pde = g_page_directory[directory];
        unsigned int* table;
        if (pde & PAGE_LARGE) {
            void* allocation = malloc(PAGE_SIZE * 2u - 1u);
            if (!allocation) return -1;
            unsigned int table_address = ((unsigned int)allocation + PAGE_SIZE - 1u) & PAGE_MASK;
            table = (unsigned int*)table_address;
            unsigned int base = directory * LARGE_PAGE_SIZE;
            for (unsigned int i = 0; i < 1024; ++i)
                table[i] = (base + i * PAGE_SIZE) | PAGE_PRESENT | PAGE_WRITE;
            g_page_directory[directory] = table_address | PAGE_PRESENT | PAGE_WRITE;
        } else {
            if (!(pde & PAGE_PRESENT)) return -1;
            table = (unsigned int*)(pde & PAGE_MASK);
        }
        table[page] |= PAGE_PWT | PAGE_PCD;
        if (page_address == last) break;
    }
    paging_flush_tlb();
    return 0;
}

int paging_set_user_range(unsigned int address, unsigned int length, int user) {
    if (!g_paging_ready || length == 0 || address >= MANAGED_LIMIT ||
        length > MANAGED_LIMIT - address) return -1;

    unsigned int first = address & PAGE_MASK;
    unsigned int end = (address + length + PAGE_SIZE - 1u) & PAGE_MASK;
    if (end > MANAGED_LIMIT || end < address) return -1;

    for (unsigned int page_address = first; page_address < end; page_address += PAGE_SIZE) {
        unsigned int directory = page_address / LARGE_PAGE_SIZE;
        unsigned int page = (page_address % LARGE_PAGE_SIZE) / PAGE_SIZE;
        unsigned int entry = g_low_page_tables[directory * 1024u + page];
        if (user) entry |= PAGE_USER;
        else entry &= ~PAGE_USER;
        g_low_page_tables[directory * 1024u + page] = entry;
    }

    /* User access is permitted only when both the PDE and PTE have U/S set. */
    unsigned int first_directory = first / LARGE_PAGE_SIZE;
    unsigned int last_directory = (end - 1u) / LARGE_PAGE_SIZE;
    for (unsigned int directory = first_directory; directory <= last_directory; directory++) {
        int has_user_page = 0;
        for (unsigned int page = 0; page < 1024; page++) {
            if (g_low_page_tables[directory * 1024u + page] & PAGE_USER) {
                has_user_page = 1;
                break;
            }
        }
        if (has_user_page) g_page_directory[directory] |= PAGE_USER;
        else g_page_directory[directory] &= ~PAGE_USER;
    }

    paging_flush_tlb();
    return 0;
}

int paging_user_range_valid(unsigned int address, unsigned int length) {
    if (!g_paging_ready || length == 0 || address >= MANAGED_LIMIT ||
        length > MANAGED_LIMIT - address) return 0;
    unsigned int first = address & PAGE_MASK;
    unsigned int end = (address + length + PAGE_SIZE - 1u) & PAGE_MASK;
    if (end > MANAGED_LIMIT || end < address) return 0;
    for (unsigned int page_address = first; page_address < end; page_address += PAGE_SIZE) {
        unsigned int directory = page_address / LARGE_PAGE_SIZE;
        unsigned int page = (page_address % LARGE_PAGE_SIZE) / PAGE_SIZE;
        if (!(g_page_directory[directory] & PAGE_USER) ||
            !(g_low_page_tables[directory * 1024u + page] & PAGE_USER)) return 0;
    }
    return 1;
}
