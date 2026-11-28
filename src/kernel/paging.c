#include "paging.h"
#include "heap.h"
#include "cykastd.h"

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
#define USER_VIRT_LIMIT 0x40000000u /* 1 GiB per-process user virtual address limit */
#define EXT_DIR_START 8u
#define EXT_DIR_COUNT 248u /* PDEs 8..255 cover 32 MiB..1 GiB */

#define PMM_MAX_PAGES 65536u /* Up to 256 MiB of physical page frames */

struct paging_space {
    unsigned int* page_directory;
    unsigned int* low_page_tables;
    unsigned int cr3;
    void* allocation;
    unsigned int* ext_tables[EXT_DIR_COUNT];
    void* ext_allocs[EXT_DIR_COUNT];
};

/* Keep the low 32 MiB in 4 KiB pages so app mappings can be isolated per process. */
static unsigned int* g_page_directory;
static unsigned int* g_low_page_tables;
static void* g_page_structure_allocation;
static int g_paging_ready;
static paging_space_t* g_active_space;

static unsigned int g_pmm_base;
static unsigned int g_pmm_page_count;
static unsigned char* g_pmm_bitmap;
static unsigned int g_pmm_hint;
static int g_has_pse_window;

#define KPHYS_WINDOW_BASE 0x80000000u /* Maps physical 0..512 MiB supervisor-only */

static inline void* phys_to_kvirt(unsigned int phys) {
    return (void*)(g_has_pse_window ? (KPHYS_WINDOW_BASE + phys) : phys);
}

static void pmm_init(void) {
    unsigned int usable_end = heap_get_usable_memory_end() & PAGE_MASK;
    unsigned int base = 0;
    if (usable_end > 0x03000000u) {
        base = 0x03000000u; /* Start above 48 MiB (preserves low 32 MiB + 16 MiB live ramdisk) */
    } else if (usable_end > 0x01800000u) {
        base = 0x01800000u; /* Fallback for smaller VMs */
    } else {
        g_pmm_base = 0;
        g_pmm_page_count = 0;
        g_pmm_bitmap = (void*)0;
        return;
    }

    unsigned int pages = (usable_end - base) / PAGE_SIZE;
    if (pages > PMM_MAX_PAGES) pages = PMM_MAX_PAGES;
    unsigned int bitmap_bytes = (pages + 7u) / 8u;
    g_pmm_bitmap = (unsigned char*)malloc(bitmap_bytes);
    if (!g_pmm_bitmap) {
        g_pmm_base = 0;
        g_pmm_page_count = 0;
        return;
    }
    memset(g_pmm_bitmap, 0, bitmap_bytes);
    g_pmm_base = base;
    g_pmm_page_count = pages;
    g_pmm_hint = 0;
}

static unsigned int pmm_alloc_page(void) {
    if (!g_pmm_bitmap || !g_pmm_page_count) return 0;
    for (unsigned int n = 0; n < g_pmm_page_count; n++) {
        unsigned int i = g_pmm_hint + n;
        if (i >= g_pmm_page_count) i -= g_pmm_page_count;
        if ((g_pmm_bitmap[i >> 3] & (1u << (i & 7u))) == 0) {
            g_pmm_bitmap[i >> 3] |= (unsigned char)(1u << (i & 7u));
            g_pmm_hint = (i + 1u < g_pmm_page_count) ? (i + 1u) : 0u;
            unsigned int addr = g_pmm_base + i * PAGE_SIZE;
            memset(phys_to_kvirt(addr), 0, PAGE_SIZE);
            return addr;
        }
    }
    return 0;
}

static void pmm_free_page(unsigned int addr) {
    if (!g_pmm_bitmap || !g_pmm_page_count || addr < g_pmm_base) return;
    unsigned int idx = (addr - g_pmm_base) / PAGE_SIZE;
    if (idx < g_pmm_page_count) {
        g_pmm_bitmap[idx >> 3] &= (unsigned char)~(1u << (idx & 7u));
        if (idx < g_pmm_hint) g_pmm_hint = idx;
    }
}

static int pmm_owns_page(unsigned int addr) {
    if (!g_pmm_bitmap || !g_pmm_page_count || addr < g_pmm_base) return 0;
    unsigned int idx = (addr - g_pmm_base) / PAGE_SIZE;
    return idx < g_pmm_page_count;
}

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

    pmm_init();

    /* The heap is initialized before paging; keep the 36 KiB tables out of
       the already crowded kernel BSS and retain their allocation forever. */
    g_page_structure_allocation = malloc(9u * PAGE_SIZE + PAGE_SIZE - 1u);
    if (!g_page_structure_allocation) return -1;
    unsigned int aligned = ((unsigned int)g_page_structure_allocation + PAGE_SIZE - 1u) & PAGE_MASK;
    g_page_directory = (unsigned int*)aligned;
    g_low_page_tables = (unsigned int*)(aligned + PAGE_SIZE);

    int has_pse = paging_cpu_has_pse();
    g_has_pse_window = has_pse;
    for (unsigned int directory = 0; directory < 1024; directory++) {
        if (has_pse) {
            unsigned int base = directory * LARGE_PAGE_SIZE;
            g_page_directory[directory] = base | PAGE_PRESENT | PAGE_WRITE | PAGE_LARGE;
        } else {
            g_page_directory[directory] = 0;
        }
    }
    if (has_pse) {
        /* Map physical 0..512 MiB at 0x80000000..0xA0000000 (supervisor-only)
         * so PMM zeroing/cloning never collides with user virtual mappings. */
        for (unsigned int d = 0; d < 128u; d++) {
            g_page_directory[512u + d] = (d * LARGE_PAGE_SIZE) | PAGE_PRESENT | PAGE_WRITE | PAGE_LARGE;
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
    g_active_space = (void*)0;
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
        if (g_active_space && directory >= 8u) {
            g_active_space->page_directory[directory] = g_page_directory[directory];
        }
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
            if (g_active_space && directory >= 8u) {
                g_active_space->page_directory[directory] = g_page_directory[directory];
            }
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

    unsigned int* pd = g_active_space ? g_active_space->page_directory : g_page_directory;
    unsigned int* pt = g_active_space ? g_active_space->low_page_tables : g_low_page_tables;

    unsigned int first = address & PAGE_MASK;
    unsigned int end = (address + length + PAGE_SIZE - 1u) & PAGE_MASK;
    if (end > MANAGED_LIMIT || end < address) return -1;

    for (unsigned int page_address = first; page_address < end; page_address += PAGE_SIZE) {
        unsigned int directory = page_address / LARGE_PAGE_SIZE;
        unsigned int page = (page_address % LARGE_PAGE_SIZE) / PAGE_SIZE;
        unsigned int entry = pt[directory * 1024u + page];
        if (user) entry |= PAGE_USER;
        else entry &= ~PAGE_USER;
        pt[directory * 1024u + page] = entry;
    }

    /* User access is permitted only when both the PDE and PTE have U/S set. */
    unsigned int first_directory = first / LARGE_PAGE_SIZE;
    unsigned int last_directory = (end - 1u) / LARGE_PAGE_SIZE;
    for (unsigned int directory = first_directory; directory <= last_directory; directory++) {
        int has_user_page = 0;
        for (unsigned int page = 0; page < 1024; page++) {
            if (pt[directory * 1024u + page] & PAGE_USER) {
                has_user_page = 1;
                break;
            }
        }
        if (has_user_page) pd[directory] |= PAGE_USER;
        else pd[directory] &= ~PAGE_USER;
    }

    paging_flush_tlb();
    return 0;
}

int paging_user_range_valid(unsigned int address, unsigned int length) {
    if (!g_paging_ready || length == 0 || address >= USER_VIRT_LIMIT ||
        length > USER_VIRT_LIMIT - address) return 0;

    unsigned int* pd = g_active_space ? g_active_space->page_directory : g_page_directory;
    unsigned int* pt = g_active_space ? g_active_space->low_page_tables : g_low_page_tables;

    unsigned int first = address & PAGE_MASK;
    unsigned int end = (address + length + PAGE_SIZE - 1u) & PAGE_MASK;
    if (end > USER_VIRT_LIMIT || end < address) return 0;
    for (unsigned int page_address = first; page_address < end; page_address += PAGE_SIZE) {
        unsigned int directory = page_address / LARGE_PAGE_SIZE;
        unsigned int page = (page_address % LARGE_PAGE_SIZE) / PAGE_SIZE;
        if (!(pd[directory] & PAGE_USER)) return 0;
        if (directory < EXT_DIR_START) {
            if (!(pt[directory * 1024u + page] & PAGE_USER)) return 0;
        } else {
            if (!g_active_space) return 0;
            unsigned int* ext = g_active_space->ext_tables[directory - EXT_DIR_START];
            if (!ext || !(ext[page] & PAGE_USER)) return 0;
        }
    }
    return 1;
}

paging_space_t* paging_create_address_space(void) {
    if (!g_paging_ready) return (void*)0;

    paging_space_t* space = (paging_space_t*)malloc(sizeof(paging_space_t));
    if (!space) return (void*)0;

    void* alloc = malloc(9u * PAGE_SIZE + PAGE_SIZE - 1u);
    if (!alloc) {
        free(space);
        return (void*)0;
    }

    unsigned int aligned = ((unsigned int)alloc + PAGE_SIZE - 1u) & PAGE_MASK;
    memset(space, 0, sizeof(*space));
    space->allocation = alloc;
    space->page_directory = (unsigned int*)aligned;
    space->low_page_tables = (unsigned int*)(aligned + PAGE_SIZE);
    space->cr3 = aligned;

    /* Mirror kernel/MMIO PDEs above 32 MiB from the template directory. */
    for (unsigned int dir = 0; dir < 1024u; dir++) {
        space->page_directory[dir] = g_page_directory[dir] & ~PAGE_USER;
    }

    /* Clone the low 32 MiB 4 KiB page tables as supervisor-only. */
    for (unsigned int table = 0; table < 8u; table++) {
        for (unsigned int page = 0; page < 1024u; page++) {
            unsigned int idx = table * 1024u + page;
            space->low_page_tables[idx] = g_low_page_tables[idx] & ~PAGE_USER;
        }
        space->page_directory[table] = (unsigned int)(space->low_page_tables + table * 1024u) |
                                       PAGE_PRESENT | PAGE_WRITE;
    }

    return space;
}

void paging_destroy_address_space(paging_space_t* space) {
    if (!space) return;
    if (g_active_space == space) {
        paging_switch_address_space((void*)0);
    }

    for (unsigned int i = 0; i < 8u * 1024u; i++) {
        unsigned int pte = space->low_page_tables[i];
        if (pte & PAGE_USER) {
            unsigned int frame = pte & PAGE_MASK;
            if (pmm_owns_page(frame)) {
                pmm_free_page(frame);
            }
        }
    }

    for (unsigned int d = 0; d < EXT_DIR_COUNT; d++) {
        unsigned int* ext = space->ext_tables[d];
        if (!ext) continue;
        for (unsigned int p = 0; p < 1024u; p++) {
            unsigned int pte = ext[p];
            if (pte & PAGE_USER) {
                unsigned int frame = pte & PAGE_MASK;
                if (pmm_owns_page(frame)) {
                    pmm_free_page(frame);
                }
            }
        }
        free(space->ext_allocs[d]);
    }

    free(space->allocation);
    free(space);
}

static unsigned int* paging_get_or_alloc_ext_table(paging_space_t* space, unsigned int directory, int create) {
    if (directory < EXT_DIR_START || directory >= EXT_DIR_START + EXT_DIR_COUNT) return (void*)0;
    unsigned int idx = directory - EXT_DIR_START;
    if (space->ext_tables[idx]) return space->ext_tables[idx];
    if (!create) return (void*)0;

    void* alloc = malloc(PAGE_SIZE * 2u - 1u);
    if (!alloc) return (void*)0;
    unsigned int aligned = ((unsigned int)alloc + PAGE_SIZE - 1u) & PAGE_MASK;
    unsigned int* table = (unsigned int*)aligned;
    memset(table, 0, PAGE_SIZE);
    space->ext_allocs[idx] = alloc;
    space->ext_tables[idx] = table;
    space->page_directory[directory] = aligned | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    return table;
}

void paging_switch_address_space(paging_space_t* space) {
    if (!g_paging_ready) return;
    g_active_space = space;
    unsigned int target_cr3 = space ? space->cr3 : (unsigned int)g_page_directory;
    unsigned int current_cr3;
    __asm__ volatile("movl %%cr3, %0" : "=r"(current_cr3));
    if (current_cr3 != target_cr3) {
        __asm__ volatile("movl %0, %%cr3" : : "r"(target_cr3) : "memory");
    }
}

int paging_alloc_user_range(paging_space_t* space, unsigned int address, unsigned int length) {
    if (!g_paging_ready || !space || length == 0 ||
        address >= USER_VIRT_LIMIT || length > USER_VIRT_LIMIT - address) return -1;

    unsigned int first = address & PAGE_MASK;
    unsigned int end = (address + length + PAGE_SIZE - 1u) & PAGE_MASK;
    if (end > USER_VIRT_LIMIT || end < address) return -1;

    for (unsigned int page_address = first; page_address < end; page_address += PAGE_SIZE) {
        unsigned int directory = page_address / LARGE_PAGE_SIZE;
        unsigned int page = (page_address % LARGE_PAGE_SIZE) / PAGE_SIZE;

        if (directory < EXT_DIR_START) {
            unsigned int idx = directory * 1024u + page;
            if (!(space->low_page_tables[idx] & PAGE_USER)) {
                unsigned int frame = pmm_alloc_page();
                if (frame) {
                    space->low_page_tables[idx] = frame | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
                } else {
                    /* Fallback to identity-mapped frame if PMM is exhausted */
                    space->low_page_tables[idx] |= PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
                }
            }
            space->page_directory[directory] |= PAGE_USER;
        } else {
            unsigned int* ext = paging_get_or_alloc_ext_table(space, directory, 1);
            if (!ext) return -1;
            if (!(ext[page] & PAGE_USER)) {
                unsigned int frame = pmm_alloc_page();
                if (!frame) return -1;
                ext[page] = frame | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
            }
            space->page_directory[directory] |= PAGE_USER;
        }
    }

    if (g_active_space == space) {
        paging_flush_tlb();
    }
    return 0;
}

int paging_free_user_range(paging_space_t* space, unsigned int address, unsigned int length) {
    if (!g_paging_ready || !space || length == 0 ||
        address >= USER_VIRT_LIMIT || length > USER_VIRT_LIMIT - address) return -1;

    unsigned int first = address & PAGE_MASK;
    unsigned int end = (address + length + PAGE_SIZE - 1u) & PAGE_MASK;
    if (end > USER_VIRT_LIMIT || end < address) return -1;

    for (unsigned int page_address = first; page_address < end; page_address += PAGE_SIZE) {
        unsigned int directory = page_address / LARGE_PAGE_SIZE;
        unsigned int page = (page_address % LARGE_PAGE_SIZE) / PAGE_SIZE;
        if (directory < EXT_DIR_START) {
            unsigned int idx = directory * 1024u + page;
            unsigned int pte = space->low_page_tables[idx];
            if (pte & PAGE_USER) {
                unsigned int frame = pte & PAGE_MASK;
                if (pmm_owns_page(frame)) {
                    pmm_free_page(frame);
                }
                space->low_page_tables[idx] = page_address | PAGE_PRESENT | PAGE_WRITE;
            }
        } else {
            unsigned int* ext = paging_get_or_alloc_ext_table(space, directory, 0);
            if (!ext) continue;
            unsigned int pte = ext[page];
            if (pte & PAGE_USER) {
                unsigned int frame = pte & PAGE_MASK;
                if (pmm_owns_page(frame)) {
                    pmm_free_page(frame);
                }
                ext[page] = 0;
            }
        }
    }

    if (g_active_space == space) {
        paging_flush_tlb();
    }
    return 0;
}

paging_space_t* paging_get_active_address_space(void) {
    return g_active_space;
}

paging_space_t* paging_clone_address_space(paging_space_t* src) {
    if (!g_paging_ready || !src) return (void*)0;
    paging_space_t* dst = paging_create_address_space();
    if (!dst) return (void*)0;

    for (unsigned int dir = 0; dir < EXT_DIR_START; dir++) {
        if (!(src->page_directory[dir] & PAGE_USER)) continue;
        dst->page_directory[dir] |= PAGE_USER;
        for (unsigned int page = 0; page < 1024u; page++) {
            unsigned int idx = dir * 1024u + page;
            unsigned int pte = src->low_page_tables[idx];
            if (!(pte & PAGE_USER)) continue;
            unsigned int src_frame = pte & PAGE_MASK;
            unsigned int dst_frame = pmm_alloc_page();
            if (!dst_frame) {
                paging_destroy_address_space(dst);
                return (void*)0;
            }
            memcpy(phys_to_kvirt(dst_frame), phys_to_kvirt(src_frame), PAGE_SIZE);
            dst->low_page_tables[idx] = dst_frame | (pte & ~PAGE_MASK);
        }
    }

    for (unsigned int d = 0; d < EXT_DIR_COUNT; d++) {
        unsigned int* src_ext = src->ext_tables[d];
        if (!src_ext) continue;
        unsigned int* dst_ext = paging_get_or_alloc_ext_table(dst, EXT_DIR_START + d, 1);
        if (!dst_ext) {
            paging_destroy_address_space(dst);
            return (void*)0;
        }
        for (unsigned int page = 0; page < 1024u; page++) {
            unsigned int pte = src_ext[page];
            if (!(pte & PAGE_USER)) continue;
            unsigned int src_frame = pte & PAGE_MASK;
            unsigned int dst_frame = pmm_alloc_page();
            if (!dst_frame) {
                paging_destroy_address_space(dst);
                return (void*)0;
            }
            memcpy(phys_to_kvirt(dst_frame), phys_to_kvirt(src_frame), PAGE_SIZE);
            dst_ext[page] = dst_frame | (pte & ~PAGE_MASK);
        }
    }
    return dst;
}
