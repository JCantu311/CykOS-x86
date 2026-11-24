#include "cykastd.h"
#include "exports.h"
#include "fat16.h"
#include "vfs.h"
#include "idt.h"
#include "module.h"
#include "paging.h"
#include "pci.h"

typedef struct {
    const char* name;
    unsigned int address;
} kernel_export_t;

#define KERNEL_EXPORT(symbol) { #symbol, (unsigned int)(symbol) }

/* Keep this table to the stable functions intended for application imports. */
static const kernel_export_t g_kernel_exports[] = {
    KERNEL_EXPORT(clear_screen),
    KERNEL_EXPORT(enable_hardware_cursor),
    KERNEL_EXPORT(print_char),
    KERNEL_EXPORT(print_string),
    KERNEL_EXPORT(print_color_string),
    KERNEL_EXPORT(set_cursor_pos),
    KERNEL_EXPORT(get_cursor_pos),
    KERNEL_EXPORT(secwait),
    KERNEL_EXPORT(msecwait),
    KERNEL_EXPORT(inb),
    KERNEL_EXPORT(outb),
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
    KERNEL_EXPORT(fat16_read_file_by_name),
    KERNEL_EXPORT(fat16_write_file),
    KERNEL_EXPORT(fat16_init),
    KERNEL_EXPORT(fat16_mount_extra_drives),
    KERNEL_EXPORT(vfs_read_file_by_name),
    KERNEL_EXPORT(vfs_write_file),
    KERNEL_EXPORT(idt_get_millis),
    KERNEL_EXPORT(keyboard_irq_has_scancode),
    KERNEL_EXPORT(keyboard_irq_read_scancode),
    KERNEL_EXPORT(keyboard_irq_inject_scancode),
    KERNEL_EXPORT(paging_map_mmio_range),
    KERNEL_EXPORT(pci_read_config_byte),
    KERNEL_EXPORT(pci_read_config_word),
    KERNEL_EXPORT(pci_read_config_dword),
    KERNEL_EXPORT(pci_write_config_word),
    KERNEL_EXPORT(pci_write_config_dword),
};

/* Keep hardware-module imports in the kernel image without exposing privileged
 * PCI/MMIO helpers through the user application symbol resolver above. */
void kernel_module_services_keepalive(void) {
    volatile unsigned int services[] = {
        (unsigned int)idt_get_millis,
        (unsigned int)keyboard_irq_inject_scancode,
        (unsigned int)paging_map_mmio_range,
        (unsigned int)pci_read_config_byte,
        (unsigned int)pci_read_config_word,
        (unsigned int)pci_read_config_dword,
        (unsigned int)pci_write_config_word,
        (unsigned int)pci_write_config_dword,
    };
    (void)services[0];
}

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
