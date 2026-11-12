#include "heap.h"
#include "cykastd.h"

#define HEAP_MAGIC 0xCAFE1234
#define ALIGN8(x) (((x) + 7) & ~7)

typedef struct heap_block {
    unsigned int magic;
    size_t size;               // Usable payload size in bytes
    int is_free;
    struct heap_block* next;
    struct heap_block* prev;
} heap_block_t;

#define BLOCK_HEADER_SIZE ALIGN8(sizeof(heap_block_t))

static unsigned int g_heap_break = HEAP_START_ADDR;
static heap_block_t* g_head = (void*)0;

void heap_init(void) {
    g_heap_break = HEAP_START_ADDR;
    g_head = (void*)0;

    // Allocate initial 64 KB heap page
    sbrk(65536);

    g_head = (heap_block_t*)HEAP_START_ADDR;
    g_head->magic = HEAP_MAGIC;
    g_head->size = 65536 - BLOCK_HEADER_SIZE;
    g_head->is_free = 1;
    g_head->next = (void*)0;
    g_head->prev = (void*)0;
}

void* sbrk(int increment) {
    unsigned int old_break = g_heap_break;

    if (increment == 0) {
        return (void*)g_heap_break;
    }

    if (increment > 0) {
        if (g_heap_break + (unsigned int)increment > HEAP_MAX_ADDR) {
            return (void*)-1; // Out of memory
        }
        g_heap_break += increment;
    } else {
        unsigned int dec = (unsigned int)(-increment);
        if (g_heap_break - dec < HEAP_START_ADDR) {
            return (void*)-1;
        }
        g_heap_break -= dec;
    }

    return (void*)old_break;
}

void* malloc(size_t size) {
    if (size == 0) return (void*)0;

    size = ALIGN8(size);

    heap_block_t* curr = g_head;
    heap_block_t* best = (void*)0;

    // Search for first suitable free block
    while (curr) {
        if (curr->magic != HEAP_MAGIC) {
            // Heap corruption detected, re-initialize
            heap_init();
            return malloc(size);
        }

        if (curr->is_free && curr->size >= size) {
            best = curr;
            break;
        }
        curr = curr->next;
    }

    // If no block found, expand the heap via sbrk
    if (!best) {
        size_t needed = BLOCK_HEADER_SIZE + size;
        if (needed < 65536) needed = 65536;

        void* new_mem = sbrk(needed);
        if (new_mem == (void*)-1) {
            return (void*)0; // Out of memory
        }

        best = (heap_block_t*)new_mem;
        best->magic = HEAP_MAGIC;
        best->size = needed - BLOCK_HEADER_SIZE;
        best->is_free = 1;
        best->next = (void*)0;

        // Append to tail
        heap_block_t* last = g_head;
        while (last && last->next) {
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
    if (best->size >= size + BLOCK_HEADER_SIZE + 16) {
        heap_block_t* split = (heap_block_t*)((unsigned char*)best + BLOCK_HEADER_SIZE + size);
        split->magic = HEAP_MAGIC;
        split->size = best->size - size - BLOCK_HEADER_SIZE;
        split->is_free = 1;
        split->next = best->next;
        split->prev = best;

        if (best->next) {
            best->next->prev = split;
        }
        best->next = split;
        best->size = size;
    }

    best->is_free = 0;
    return (void*)((unsigned char*)best + BLOCK_HEADER_SIZE);
}

void free(void* ptr) {
    if (!ptr) return;

    heap_block_t* block = (heap_block_t*)((unsigned char*)ptr - BLOCK_HEADER_SIZE);
    if (block->magic != HEAP_MAGIC) {
        return; // Corrupt pointer, ignore
    }

    block->is_free = 1;

    // Coalesce with next block if free
    if (block->next && block->next->is_free && block->next->magic == HEAP_MAGIC) {
        block->size += BLOCK_HEADER_SIZE + block->next->size;
        block->next = block->next->next;
        if (block->next) {
            block->next->prev = block;
        }
    }

    // Coalesce with previous block if free
    if (block->prev && block->prev->is_free && block->prev->magic == HEAP_MAGIC) {
        block->prev->size += BLOCK_HEADER_SIZE + block->size;
        block->prev->next = block->next;
        if (block->next) {
            block->next->prev = block->prev;
        }
    }
}

void* realloc(void* ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) {
        free(ptr);
        return (void*)0;
    }

    heap_block_t* block = (heap_block_t*)((unsigned char*)ptr - BLOCK_HEADER_SIZE);
    if (block->magic != HEAP_MAGIC) return (void*)0;

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
