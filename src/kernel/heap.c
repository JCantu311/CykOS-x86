#include "heap.h"
#include "cykastd.h"

#define HEAP_MAGIC 0xCAFE1234
#define HEAP_ALIGNMENT 8u
#define E820_COUNT_ADDRESS 0x04FC
#define E820_MAP_ADDRESS   0x0500
#define E820_ENTRY_LIMIT   20

typedef struct __attribute__((packed)) {
    unsigned int base_low;
    unsigned int base_high;
    unsigned int length_low;
    unsigned int length_high;
    unsigned int type;
    unsigned int attributes;
} heap_e820_entry_t;

typedef struct heap_block {
    unsigned int magic;
    size_t size;               // Usable payload size in bytes
    int is_free;
    unsigned int owner;
    struct heap_block* next;
    struct heap_block* prev;
} heap_block_t;

#define BLOCK_HEADER_SIZE ((sizeof(heap_block_t) + 7u) & ~7u)

static unsigned int g_heap_break = HEAP_START_ADDR;
static unsigned int g_heap_max_addr = HEAP_FALLBACK_MAX_ADDR;
static unsigned int g_usable_memory_end = 0x01000000u;
static heap_block_t* g_head = (void*)0;
static unsigned int g_heap_allocation_owner;
static int g_heap_corrupt;

static int heap_align_size(size_t size, size_t* aligned) {
    if (size > 0xFFFFFFFFu - (HEAP_ALIGNMENT - 1u)) return -1;
    *aligned = (size + (HEAP_ALIGNMENT - 1u)) & ~(HEAP_ALIGNMENT - 1u);
    return 0;
}

static int heap_block_address_valid(const heap_block_t* block) {
    unsigned int address = (unsigned int)block;
    return g_heap_break >= HEAP_START_ADDR + BLOCK_HEADER_SIZE &&
           address >= HEAP_START_ADDR && address <= g_heap_break - BLOCK_HEADER_SIZE &&
           (address & (HEAP_ALIGNMENT - 1u)) == 0;
}

static int heap_validate_block(const heap_block_t* block) {
    if (!heap_block_address_valid(block) || block->magic != HEAP_MAGIC) return -1;
    unsigned int address = (unsigned int)block;
    unsigned int payload = address + BLOCK_HEADER_SIZE;
    if (block->size > g_heap_break - payload) return -1;
    if (block->is_free != 0 && block->is_free != 1) return -1;
    unsigned int end = payload + block->size;
    if (block->next) {
        if (!heap_block_address_valid(block->next) || (unsigned int)block->next < end)
            return -1;
    } else if (end > g_heap_break) {
        return -1;
    }
    return 0;
}

extern void* usercmd_app_sbrk(int increment);
extern int usercmd_is_app_started(void);
static void* heap_sbrk_raw(int increment);

void heap_set_allocation_owner(unsigned int owner) {
    g_heap_allocation_owner = owner;
}

void heap_free_allocations_by_owner(unsigned int owner) {
    if (owner == 0 || g_heap_corrupt) return;

    heap_block_t* block = g_head;
    heap_block_t* previous = (void*)0;
    while (block) {
        if (heap_validate_block(block) != 0 || block->prev != previous ||
            (block->next && !heap_block_address_valid(block->next))) {
            g_heap_corrupt = 1;
            return;
        }
        if (!block->is_free && block->owner == owner) {
            block->is_free = 1;
            block->owner = 0;
        }
        previous = block;
        block = block->next;
    }

    block = g_head;
    while (block && block->next) {
        if (heap_validate_block(block) != 0 ||
            heap_validate_block(block->next) != 0 || block->next->prev != block) {
            g_heap_corrupt = 1;
            return;
        }
        if (block->is_free && block->next->is_free &&
            block->next->magic == HEAP_MAGIC) {
            if (block->size > 0xFFFFFFFFu - BLOCK_HEADER_SIZE - block->next->size) {
                g_heap_corrupt = 1;
                return;
            }
            block->size += BLOCK_HEADER_SIZE + block->next->size;
            block->next = block->next->next;
            if (block->next) {
                if (!heap_block_address_valid(block->next)) { g_heap_corrupt = 1; return; }
                block->next->prev = block;
            }
        } else {
            block = block->next;
        }
    }
}

static unsigned int detect_contiguous_usable_end(unsigned int limit,
                                                 unsigned int fallback) {
    volatile unsigned int* count_ptr = (volatile unsigned int*)E820_COUNT_ADDRESS;
    heap_e820_entry_t* entries = (heap_e820_entry_t*)E820_MAP_ADDRESS;
    unsigned int count = *count_ptr;
    unsigned int covered = HEAP_START_ADDR;

    if (count > E820_ENTRY_LIMIT) count = E820_ENTRY_LIMIT;
    if (count == 0) return fallback;

    // Extend only across contiguous usable ranges. This avoids allocating
    // through an E820 reserved hole when firmware splits the memory map.
    for (unsigned int pass = 0; pass < count; pass++) {
        unsigned int previous = covered;
        for (unsigned int i = 0; i < count; i++) {
            heap_e820_entry_t* entry = &entries[i];
            if (entry->type != 1 || !(entry->attributes & 1) ||
                entry->base_high != 0 || entry->length_low == 0) continue;

            unsigned int start = entry->base_low;
            unsigned int end;
            if (entry->length_high != 0 || entry->length_low > 0xFFFFFFFFu - start)
                end = limit;
            else
                end = start + entry->length_low;

            if (end > limit) end = limit;
            if (start <= covered && end > covered) covered = end;
        }
        if (covered == previous || covered >= limit) break;
    }

    return covered > HEAP_START_ADDR ? covered : fallback;
}

unsigned int heap_get_max_addr(void) {
    return g_heap_max_addr;
}

unsigned int heap_get_usable_memory_end(void) {
    return g_usable_memory_end;
}

void heap_init(void) {
    // Initialization is a boot-time operation. Never erase allocator state if
    // called again after allocations have been made.
    if (g_head && g_heap_break > HEAP_START_ADDR) {
        g_heap_corrupt = 1;
        return;
    }
    g_usable_memory_end = detect_contiguous_usable_end(0x02000000u, 0x01000000u);
    g_heap_max_addr = detect_contiguous_usable_end(HEAP_MAX_ADDR,
                                                    HEAP_FALLBACK_MAX_ADDR);
    if (g_heap_max_addr > HEAP_MAX_ADDR) g_heap_max_addr = HEAP_MAX_ADDR;
    if (g_heap_max_addr < HEAP_START_ADDR + 65536)
        g_heap_max_addr = HEAP_FALLBACK_MAX_ADDR;
    g_heap_break = HEAP_START_ADDR;
    g_head = (void*)0;
    g_heap_corrupt = 0;

    // Allocate initial 64 KB heap page
    if (heap_sbrk_raw(65536) == (void*)-1) {
        g_heap_corrupt = 1;
        return;
    }

    g_head = (heap_block_t*)HEAP_START_ADDR;
    g_head->magic = HEAP_MAGIC;
    g_head->size = 65536 - BLOCK_HEADER_SIZE;
    g_head->is_free = 1;
    g_head->owner = 0;
    g_head->next = (void*)0;
    g_head->prev = (void*)0;
}

static void* heap_sbrk_raw(int increment) {
    unsigned int old_break = g_heap_break;

    if (increment == 0) {
        return (void*)g_heap_break;
    }

    if (increment > 0) {
        if (g_heap_break > g_heap_max_addr ||
            (unsigned int)increment > g_heap_max_addr - g_heap_break) {
            return (void*)-1; // Out of memory
        }
        g_heap_break += increment;
    } else {
        unsigned int dec = 0u - (unsigned int)increment;
        if (g_heap_break < HEAP_START_ADDR || dec > g_heap_break - HEAP_START_ADDR) {
            return (void*)-1;
        }
        g_heap_break -= dec;
    }

    return (void*)old_break;
}

void* sbrk(int increment) {
    if (g_heap_allocation_owner != 0 && usercmd_is_app_started())
        return usercmd_app_sbrk(increment);
    return heap_sbrk_raw(increment);
}

void* malloc(size_t size) {
    if (size == 0) return (void*)0;
    if (g_heap_corrupt) return (void*)0;
    if (heap_align_size(size, &size) != 0 || size > 0xFFFFFFFFu - BLOCK_HEADER_SIZE)
        return (void*)0;

    heap_block_t* curr = g_head;
    heap_block_t* best = (void*)0;
    heap_block_t* previous = (void*)0;

    // Search for first suitable free block
    while (curr) {
        if (heap_validate_block(curr) != 0 || curr->prev != previous) {
            // Stop allocating after metadata corruption; rebuilding here could
            // hand out memory that is still owned by live allocations.
            g_heap_corrupt = 1;
            return (void*)0;
        }

        if (curr->is_free && curr->size >= size) {
            best = curr;
            break;
        }
        previous = curr;
        if (curr->next && !heap_block_address_valid(curr->next)) {
            g_heap_corrupt = 1;
            return (void*)0;
        }
        curr = curr->next;
    }

    // If no block found, expand the heap via sbrk
    if (!best) {
        size_t needed = BLOCK_HEADER_SIZE + size;
        if (needed < 65536) needed = 65536;
        if (needed > 0x7FFFFFFFu) return (void*)0;

        void* new_mem = heap_sbrk_raw(needed);
        if (new_mem == (void*)-1) {
            return (void*)0; // Out of memory
        }

        best = (heap_block_t*)new_mem;
        best->magic = HEAP_MAGIC;
        best->size = needed - BLOCK_HEADER_SIZE;
        best->is_free = 1;
        best->owner = 0;
        best->next = (void*)0;

        // Append to tail
        heap_block_t* last = g_head;
        while (last && last->next) {
            if (heap_validate_block(last) != 0 ||
                !heap_block_address_valid(last->next) ||
                last->next->prev != last) {
                g_heap_corrupt = 1;
                return (void*)0;
            }
            last = last->next;
        }
        if (last) {
            last->next = best;
            best->prev = last;
        } else {
            g_head = best;
            best->prev = (void*)0;
        }
    }

    // Split block if excess space is large enough
    if (best->size >= size && best->size - size >= BLOCK_HEADER_SIZE + 16) {
        heap_block_t* split = (heap_block_t*)((unsigned char*)best + BLOCK_HEADER_SIZE + size);
        split->magic = HEAP_MAGIC;
        split->size = best->size - size - BLOCK_HEADER_SIZE;
        split->is_free = 1;
        split->owner = 0;
        split->next = best->next;
        split->prev = best;

        if (best->next) {
            if (heap_validate_block(best->next) != 0 || best->next->prev != best) {
                g_heap_corrupt = 1;
                return (void*)0;
            }
            best->next->prev = split;
        }
        best->next = split;
        best->size = size;
    }

    best->is_free = 0;
    best->owner = g_heap_allocation_owner;
    return (void*)((unsigned char*)best + BLOCK_HEADER_SIZE);
}

void free(void* ptr) {
    if (!ptr || g_heap_corrupt) return;

    // Only accept an exact payload pointer found by walking validated blocks.
    // Do not read a presumed header before an arbitrary caller-supplied pointer.
    heap_block_t* block = g_head;
    heap_block_t* previous = (void*)0;
    while (block) {
        if (heap_validate_block(block) != 0 || block->prev != previous) {
            g_heap_corrupt = 1;
            return;
        }
        if ((void*)((unsigned char*)block + BLOCK_HEADER_SIZE) == ptr) break;
        previous = block;
        if (block->next && !heap_block_address_valid(block->next)) {
            g_heap_corrupt = 1;
            return;
        }
        block = block->next;
    }
    if (!block || block->is_free) return;

    block->is_free = 1;
    block->owner = 0;

    // Coalesce with adjacent blocks only after validating their metadata.
    if (block->next) {
        if (heap_validate_block(block->next) != 0 || block->next->prev != block) {
            g_heap_corrupt = 1;
            return;
        }
        if (block->next->is_free) {
            if (block->size > 0xFFFFFFFFu - BLOCK_HEADER_SIZE - block->next->size) {
                g_heap_corrupt = 1;
                return;
            }
            block->size += BLOCK_HEADER_SIZE + block->next->size;
            block->next = block->next->next;
            if (block->next) {
                if (!heap_block_address_valid(block->next)) { g_heap_corrupt = 1; return; }
                block->next->prev = block;
            }
        }
    }

    // Coalesce with previous block if free
    if (block->prev) {
        if (heap_validate_block(block->prev) != 0 || block->prev->next != block) {
            g_heap_corrupt = 1;
            return;
        }
        if (block->prev->is_free) {
            if (block->prev->size > 0xFFFFFFFFu - BLOCK_HEADER_SIZE - block->size) {
                g_heap_corrupt = 1;
                return;
            }
            block->prev->size += BLOCK_HEADER_SIZE + block->size;
            block->prev->next = block->next;
            if (block->next) {
                if (!heap_block_address_valid(block->next)) { g_heap_corrupt = 1; return; }
                block->next->prev = block->prev;
            }
        }
    }
}

void* realloc(void* ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) {
        free(ptr);
        return (void*)0;
    }

    if (g_heap_corrupt) return (void*)0;
    heap_block_t* block = g_head;
    while (block && (void*)((unsigned char*)block + BLOCK_HEADER_SIZE) != ptr) {
        if (heap_validate_block(block) != 0) { g_heap_corrupt = 1; return (void*)0; }
        if (block->next && !heap_block_address_valid(block->next)) {
            g_heap_corrupt = 1;
            return (void*)0;
        }
        block = block->next;
    }
    if (!block || heap_validate_block(block) != 0 || block->is_free) return (void*)0;

    if (block->size >= size) {
        return ptr; // Already large enough
    }

    void* new_ptr = malloc(size);
    if (!new_ptr) return (void*)0;

    size_t copy_size = (block->size < size) ? block->size : size;
    memcpy(new_ptr, ptr, copy_size);
    free(ptr);

    return new_ptr;
}

void* calloc(size_t num, size_t size) {
    if (num != 0 && size > 0xFFFFFFFFu / num) return (void*)0;
    size_t total = num * size;
    void* ptr = malloc(total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

size_t heap_get_used_bytes(void) {
    size_t total = 0;
    heap_block_t* curr = g_head;
    while (curr) {
        if (!curr->is_free) {
            total += curr->size;
        }
        curr = curr->next;
    }
    return total;
}

size_t heap_get_free_bytes(void) {
    size_t total = 0;
    heap_block_t* curr = g_head;
    while (curr) {
        if (curr->is_free) {
            total += curr->size;
        }
        curr = curr->next;
    }
    return total;
}
