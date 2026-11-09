#ifndef HEAP_H
#define HEAP_H

#include "cykastd.h"

// Heap base at 1MB (0x100000) - safe extended memory zone
#define HEAP_START_ADDR  0x100000
#define HEAP_MAX_ADDR    0x800000 // 8 MB ceiling (7 MB total heap)

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
