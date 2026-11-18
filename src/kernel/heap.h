#ifndef HEAP_H
#define HEAP_H

#include "cykastd.h"

// Kernel heap stays below 8 MiB; app heap pages begin at 8 MiB and are mapped
// user-accessible only up to the contiguous RAM end reported by E820.
#define HEAP_START_ADDR       0x400000
#define HEAP_MAX_ADDR         0x800000 // Keep app memory separate from kernel allocator metadata.
#define HEAP_FALLBACK_MAX_ADDR 0x800000 // Safe ceiling if E820 is unavailable

void  heap_init(void);
void* sbrk(int increment);
void* malloc(size_t size);
void  free(void* ptr);
void* realloc(void* ptr, size_t size);
void* calloc(size_t num, size_t size);
void  heap_set_allocation_owner(unsigned int owner);
void  heap_free_allocations_by_owner(unsigned int owner);

// Diagnostics
size_t heap_get_used_bytes(void);
size_t heap_get_free_bytes(void);
unsigned int heap_get_max_addr(void);
unsigned int heap_get_usable_memory_end(void);

#endif // HEAP_H
