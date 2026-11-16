#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include "syscall.h"

#define HEAP_MAGIC 0x504F5358 // "POSX"

typedef struct block {
    size_t size;
    int is_free;
    struct block* next;
    struct block* prev;
} block_t;

#define BLOCK_SIZE sizeof(block_t)

static block_t* free_list = NULL;

void* malloc(size_t size) {
    if (size == 0) return NULL;
    size = (size + 7) & ~7; // 8-byte align

    block_t* curr = free_list;
    while (curr) {
        if (curr->is_free && curr->size >= size) {
            curr->is_free = 0;
            return (void*)(curr + 1);
        }
        curr = curr->next;
    }

    // Need more memory from kernel
    size_t total_needed = size + BLOCK_SIZE;
    block_t* new_block = (block_t*)sbrk(total_needed);
    if (new_block == (void*)-1) {
        return NULL;
    }

    new_block->size = size;
    new_block->is_free = 0;
    new_block->next = NULL;
    new_block->prev = NULL;

    if (!free_list) {
        free_list = new_block;
    } else {
        block_t* tail = free_list;
        while (tail->next) tail = tail->next;
        tail->next = new_block;
        new_block->prev = tail;
    }

    return (void*)(new_block + 1);
}

void free(void* ptr) {
    if (!ptr) return;
    block_t* b = ((block_t*)ptr) - 1;
    b->is_free = 1;

    // Coalesce forward
    if (b->next && b->next->is_free) {
        b->size += BLOCK_SIZE + b->next->size;
        b->next = b->next->next;
        if (b->next) b->next->prev = b;
    }
    // Coalesce backward
    if (b->prev && b->prev->is_free) {
        b->prev->size += BLOCK_SIZE + b->size;
        b->prev->next = b->next;
        if (b->next) b->next->prev = b->prev;
    }
}

void* calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void* p = malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void* realloc(void* ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    block_t* b = ((block_t*)ptr) - 1;
    if (b->size >= size) {
        return ptr;
    }
    void* new_ptr = malloc(size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, b->size);
        free(ptr);
    }
    return new_ptr;
}

void exit(int status) {
    // Restore text mode only when the application actually changed modes.
    // Setting mode 0 clears the screen, so doing it unconditionally erases
    // ordinary console output from short-lived commands (for example `lua -v`).
    int graphics_mode = __syscall1(SYS_GFX_MODE, -1); // Query active video mode.
    if (graphics_mode > 0) {
        static const unsigned char text_palette[16 * 3] = {
              0,   0,   0,   0,   0, 170,   0, 170,   0,   0, 170, 170,
            170,   0,   0, 170,   0, 170, 170,  85,   0, 170, 170, 170,
             85,  85,  85,  85,  85, 255,  85, 255,  85,  85, 255, 255,
            255,  85,  85, 255,  85, 255, 255, 255,  85, 255, 255, 255
        };
        // The current mode's palette API permits replacing entries 0-15.
        // Restore the text colors explicitly before returning to text mode.
        (void)__syscall2(SYS_GFX_PALETTE, 16 << 16, (int)text_palette);
        (void)__syscall1(SYS_GFX_MODE, 0); // Restore text mode before flushing output.
    }
    fflush(stdout);
    fflush(stderr);
    __syscall1(1, status); // SYS_EXIT
    while (1) {
        asm volatile("hlt");
    }
}

void abort(void) {
    exit(134); // SIGABRT
}

int atoi(const char* nptr) {
    return (int)strtol(nptr, NULL, 10);
}

double atof(const char* nptr) {
    const char* s = nptr;
    while (isspace((unsigned char)*s)) s++;

    int sign = 1;
    if (*s == '-') {
        sign = -1;
        s++;
    } else if (*s == '+') {
        s++;
    }

    double value = 0.0;
    while (isdigit((unsigned char)*s)) {
        value = value * 10.0 + (*s++ - '0');
    }
    if (*s == '.') {
        s++;
        double place = 0.1;
        while (isdigit((unsigned char)*s)) {
            value += (*s++ - '0') * place;
            place *= 0.1;
        }
    }

    int exponent_sign = 1;
    int exponent = 0;
    if (*s == 'e' || *s == 'E') {
        s++;
        if (*s == '-') {
            exponent_sign = -1;
            s++;
        } else if (*s == '+') {
            s++;
        }
        while (isdigit((unsigned char)*s)) {
            if (exponent < 308) {
                exponent = exponent * 10 + (*s - '0');
                if (exponent > 308) exponent = 308;
            }
            s++;
        }
    }

    while (exponent-- > 0) value = exponent_sign > 0 ? value * 10.0 : value / 10.0;
    return sign * value;
}

long strtol(const char* nptr, char** endptr, int base) {
    const char* s = nptr;
    while (isspace((unsigned char)*s)) s++;

    int sign = 1;
    if (*s == '-') {
        sign = -1;
        s++;
    } else if (*s == '+') {
        s++;
    }

    if (base == 0) {
        if (*s == '0') {
            s++;
            if (*s == 'x' || *s == 'X') {
                base = 16;
                s++;
            } else {
                base = 8;
            }
        } else {
            base = 10;
        }
    } else if (base == 16 && *s == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }

    long result = 0;
    while (*s) {
        int val;
        if (isdigit((unsigned char)*s)) {
            val = *s - '0';
        } else if (isalpha((unsigned char)*s)) {
            val = tolower((unsigned char)*s) - 'a' + 10;
        } else {
            break;
        }
        if (val >= base) break;
        result = result * base + val;
        s++;
    }

    if (endptr) *endptr = (char*)s;
    return sign * result;
}

int abs(int j) {
    return j < 0 ? -j : j;
}

// Process creation is not implemented by CykOS yet. POSIX callers receive
// failure, allowing optional host utilities (such as Doom's Zenity probe) to
// be skipped cleanly.
int system(const char* command) {
    (void)command;
    return -1;
}

void qsort(void* base, size_t nmemb, size_t size, int (*compar)(const void*, const void*)) {
    if (nmemb <= 1) return;
    char* b = (char*)base;
    for (size_t i = 0; i < nmemb - 1; i++) {
        for (size_t j = 0; j < nmemb - 1 - i; j++) {
            char* p1 = b + j * size;
            char* p2 = b + (j + 1) * size;
            if (compar(p1, p2) > 0) {
                for (size_t k = 0; k < size; k++) {
                    char tmp = p1[k];
                    p1[k] = p2[k];
                    p2[k] = tmp;
                }
            }
        }
    }
}

void* bsearch(const void* key, const void* base, size_t nmemb, size_t size, int (*compar)(const void*, const void*)) {
    size_t l = 0, r = nmemb;
    const char* b = (const char*)base;
    while (l < r) {
        size_t mid = l + (r - l) / 2;
        int res = compar(key, b + mid * size);
        if (res == 0) return (void*)(b + mid * size);
        if (res > 0) l = mid + 1;
        else r = mid;
    }
    return NULL;
}
