#ifndef HEAP_H
#define HEAP_H

#include "cykastd.h"

// Reserve 0x100000-0x130000 for the standalone Level 3 shell image.
#define HEAP_START_ADDR  0x140000
#define HEAP_MAX_ADDR    0x800000 // 8 MB ceiling

void  heap_init(void);
void* sbrk(int increment);
void* malloc(size_t size);
void  free(void* ptr);
void* realloc(void* ptr, size_t size);
void* calloc(size_t num, size_t size);

// Diagnostics
size_t heap_get_used_bytes(void);
size_t heap_get_free_bytes(void);

#endif // HEAP_H
