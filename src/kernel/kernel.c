#include "cykastd.h"
#include "ata.h"
#include "fat16.h"
#include "vfs.h"
#include "modload.h"
#include "idt.h"
#include "syscall.h"
#include "heap.h"
#include "usercmd.h"
#include "permissions.h"
#include "paging.h"
#include "gdt.h"
#include "exports.h"
#include "ext2.h"

// Level 3 CykShell Entry Point (implemented in shell.rs)
extern void shell_main(void);

#define E820_COUNT_ADDRESS 0x04FC
#define E820_MAP_ADDRESS   0x0500
#define E820_ENTRY_LIMIT   20

typedef struct __attribute__((packed)) {
    unsigned int base_low;
    unsigned int base_high;
    unsigned int length_low;
    unsigned int length_high;
    unsigned int type;
    unsigned int attributes;
} e820_entry_t;

static void print_unsigned(unsigned int value) {
    char digits[11];
    unsigned int count = 0;
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value && count < sizeof(digits));
    while (count) print_char(digits[--count]);
}

static void run_memory_test(void) {
    volatile unsigned int* map_count = (volatile unsigned int*)E820_COUNT_ADDRESS;
    e820_entry_t* entries = (e820_entry_t*)E820_MAP_ADDRESS;
    unsigned int count = *map_count;
    unsigned int usable_mb = 0;

    if (count > E820_ENTRY_LIMIT) count = E820_ENTRY_LIMIT;
    if (count == 0) {
        print_color_string("[MEM] BIOS memory map unavailable; test skipped.\n", VGA_RED);
        return;
    }

    print_string("[MEM] Reading BIOS-reported usable RAM map...\n");
    for (unsigned int i = 0; i < count; i++) {
        e820_entry_t* entry = &entries[i];
        if (entry->type != 1 || !(entry->attributes & 1) ||
            entry->base_high != 0 || entry->length_low == 0) continue;

        unsigned int start = entry->base_low;
        unsigned int length = entry->length_low;
        unsigned int end;
        if (entry->length_high != 0 || length > 0xFFFFFFFF - start) {
            end = 0xFFFFFFFF;
        } else {
            end = start + length;
        }
        if (end <= start) continue;
        usable_mb += (end - start) >> 20;
    }

    print_string("[MEM] Usable RAM below 4 GiB: ");
    print_unsigned(usable_mb);
    print_string(" MiB (BIOS map; physical write test skipped).\n");
}

void kernel_main(void) {
    kernel_module_services_keepalive();
    gdt_init();
    run_memory_test();
    heap_init();
    /* Paging starts with an identity map, preserving the boot-time addresses. */
    if (paging_init() != 0) {
        print_color_string("[MEM] Paging structures unavailable; stopping before app startup.\n",
                           VGA_RED);
        for (;;) asm volatile("cli; hlt");
    }
    uefi_console_init();
    clear_screen();
    enable_hardware_cursor();

    // 1. Initialize CPU Interrupt Descriptor Table (Catches exceptions, prevents Triple Faults)
    idt_init();

    char msg_init[] = "CykOS Kernel Init (GDT + Paging + IDT + Syscalls [OK])\n";
    char msg_pass[] = "PASS\n";
    char msg_fail[] = "FAIL\n";

    syscall_init();

    print_string(msg_init);

    char cykaos_version[] = "CykOS v0.1.0\n";
    print_string(cykaos_version);

    char welcome_message[] = "Welcome to CykOS...\n";
    char disk_test_msg[] = "Running non-destructive Disk Check...\n";

    print_string(disk_test_msg);

    int disk_result = disk_test();

    if (disk_result == 0) {
        print_color_string(msg_pass, VGA_GREEN);
    } else {
        print_color_string(msg_fail, VGA_RED);
    }

    print_color_string(welcome_message, VGA_GREEN);

    print_string("\n[Level 1] Initializing root filesystem through VFS...\n");
    volatile unsigned int* live_boot_info = (volatile unsigned int*)0x04F0;
    if (live_boot_info[0] == 0x534F4B43u)
        print_string("[LIVE] Preparing a temporary root filesystem from the CD...\n");
    if (vfs_mount_root() == 0) {
        permissions_init();
        print_string("VFS mounted root with ");
        print_string(vfs_current_driver());
        print_string(".\n");
        if (ext2_live_status() == 2)
            print_string("[LIVE] Root is writable in RAM. All changes are lost at shutdown.\n");
        else if (ext2_live_status() == 1)
            print_string("[LIVE] RAM is insufficient for a writable root; using the CD read-only.\n");

        print_string("[Level 1] Verifying File Read [/USR/SHARE/DOC/README.TXT]...\n");
        vfs_file_t readme_file;
        if (vfs_find_file("/USR/SHARE/DOC/README.TXT", &readme_file) == 0) {
            char file_buf[128];
            int read_bytes = vfs_read_at(&readme_file, 0, file_buf, sizeof(file_buf) - 1);
            if (read_bytes > 0) {
                file_buf[read_bytes] = '\0';
                print_string(file_buf);
                print_string("\n");
            } else {
                print_color_string(" -> Error: README.TXT is empty!\n", VGA_RED);
            }
        } else {
            print_color_string(" -> README.TXT not found on disk!\n", VGA_RED);
        }
    } else {
        print_color_string("VFS: No supported filesystem detected on disk.\n", VGA_RED);
    }

    // Load Level 2 drivers from /BOOT/MODULES.CFG
    print_string("\n[Level 2] Loading Driver Modules from /BOOT/MODULES.CFG...\n");
    int modules_loaded = modload_load_all();
    if (modules_loaded > 0) {
        print_color_string("[Level 2] Driver Modules Loaded Successfully.\n", VGA_GREEN);
    } else {
        print_color_string("[Level 2] Warning: No Level 2 modules loaded.\n", VGA_RED);
    }

    /* USB host discovery runs from the USB Level 2 module init hook. */
    /* The USB mass-storage backend now participates in FAT volume discovery. */
    (void)fat16_mount_extra_drives();

    print_string("\n[Level 3] Boot sequence complete. Transitioning to CykShell...\n");
    print_string("[Level 3] Flushing boot log to /VAR/LOG/LOG.TXT...\n");
    int log_result = bootlog_flush("/VAR/LOG/LOG.TXT");
    bootlog_disable();
    if (log_result == 0) {
        print_string("[Level 3] Boot log saved.\n");
    } else {
        print_color_string("[Level 3] Boot log could not be saved (status ", VGA_YELLOW);
        if (log_result < 0) {
            print_char('-');
            print_unsigned((unsigned int)(-log_result));
        } else {
            print_unsigned((unsigned int)log_result);
        }
        print_color_string(").\n", VGA_YELLOW);
    }

    // Prefer the standalone Level 3 image; retain the kernel-linked shell fallback.
    exec_shell_binary();
    shell_main();
    
    while (1) {
        asm("hlt");
    }
}
