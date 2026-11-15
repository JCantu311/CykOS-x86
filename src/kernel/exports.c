#include "cykastd.h"
#include "exports.h"

typedef struct {
    const char* name;
    unsigned int address;
} kernel_export_t;

#define KERNEL_EXPORT(symbol) { #symbol, (unsigned int)(symbol) }

/* Keep this table to the stable functions intended for application imports. */
static const kernel_export_t g_kernel_exports[] = {
    KERNEL_EXPORT(clear_screen),
    KERNEL_EXPORT(print_char),
    KERNEL_EXPORT(print_string),
    KERNEL_EXPORT(print_color_string),
    KERNEL_EXPORT(set_cursor_pos),
    KERNEL_EXPORT(get_cursor_pos),
    KERNEL_EXPORT(secwait),
    KERNEL_EXPORT(msecwait),
    KERNEL_EXPORT(strlen),
    KERNEL_EXPORT(strncmp),
    KERNEL_EXPORT(memcpy),
    KERNEL_EXPORT(memset),
    KERNEL_EXPORT(memmove),
    KERNEL_EXPORT(memcmp),
    KERNEL_EXPORT(bcmp),
    KERNEL_EXPORT(rust_eh_personality),
    KERNEL_EXPORT(malloc),
    KERNEL_EXPORT(free),
    KERNEL_EXPORT(realloc),
    KERNEL_EXPORT(calloc),
    KERNEL_EXPORT(has_key),
    KERNEL_EXPORT(get_scancode),
    KERNEL_EXPORT(scancode_to_ascii),
    KERNEL_EXPORT(get_char),
};

int kernel_export_resolve(const char* name, unsigned int* address) {
    if (!name || !address) return -1;
    for (unsigned int i = 0; i < sizeof(g_kernel_exports) / sizeof(g_kernel_exports[0]); i++) {
        const char* a = name;
        const char* b = g_kernel_exports[i].name;
        while (*a && *a == *b) { a++; b++; }
        if (*a == '\0' && *b == '\0') {
            *address = g_kernel_exports[i].address;
            return 0;
        }
    }
    return -1;
}
