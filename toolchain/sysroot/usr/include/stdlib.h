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
int   atexit(void (*func)(void));
int   system(const char* command);

int   atoi(const char* nptr);
double atof(const char* nptr);
double strtod(const char* nptr, char** endptr);
long  strtol(const char* nptr, char** endptr, int base);
unsigned long strtoul(const char* nptr, char** endptr, int base);
long long strtoll(const char* nptr, char** endptr, int base);
unsigned long long strtoull(const char* nptr, char** endptr, int base);
long  atol(const char* nptr);
long long atoll(const char* nptr);
int   abs(int j);
long  labs(long j);

int   rand(void);
void  srand(unsigned int seed);

extern char** environ;
char* getenv(const char* name);
int   setenv(const char* name, const char* value, int overwrite);
int   unsetenv(const char* name);
int   putenv(char* string);
int   mkstemp(char* tmpl);
char* realpath(const char* path, char* resolved_path);

void  qsort(void* base, size_t nmemb, size_t size, int (*compar)(const void*, const void*));
void* bsearch(const void* key, const void* base, size_t nmemb, size_t size, int (*compar)(const void*, const void*));

#endif
