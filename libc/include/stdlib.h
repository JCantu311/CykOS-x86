#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>
#include <sys/types.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

void* malloc(size_t size);
void  free(void* ptr);
void* calloc(size_t nmemb, size_t size);
void* realloc(void* ptr, size_t size);

void  exit(int status) __attribute__((noreturn));
void  abort(void) __attribute__((noreturn));

int   atoi(const char* nptr);
long  strtol(const char* nptr, char** endptr, int base);
int   abs(int j);

void  qsort(void* base, size_t nmemb, size_t size, int (*compar)(const void*, const void*));
void* bsearch(const void* key, const void* base, size_t nmemb, size_t size, int (*compar)(const void*, const void*));

#endif
