#ifndef HEAP_H
#define HEAP_H

#include "cykastd.h"

// Fixed apps: 0x180000-0x210000; app stack: 0x210000-0x230000.
// Leave a guard gap before the kernel heap. The usable end is detected from
// the BIOS E820 map and capped to keep the low-memory layout predictable.
#define HEAP_START_ADDR       0x250000
#define HEAP_MAX_ADDR         0x1000000 // 16 MiB maximum heap window
#define HEAP_FALLBACK_MAX_ADDR 0x800000 // Safe ceiling if E820 is unavailable

void  heap_init(void);
void* sbrk(int increment);
void* malloc(size_t size);
void  free(void* ptr);
void* realloc(void* ptr, size_t size);
void* calloc(size_t num, size_t size);

// Diagnostics
size_t heap_get_used_bytes(void);
size_t heap_get_free_bytes(void);
unsigned int heap_get_max_addr(void);

#endif // HEAP_H
