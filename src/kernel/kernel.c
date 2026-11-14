#include "cykastd.h"
#include "ata.h"
#include "fat16.h"
#include "vfs.h"
#include "modload.h"
#include "idt.h"
#include "syscall.h"
#include "heap.h"
#include "usercmd.h"

// Level 3 CykShell Entry Point (implemented in shell.rs)
extern void shell_main(void);

#define E820_COUNT_ADDRESS 0x04FC
#define E820_MAP_ADDRESS   0x0500
#define E820_ENTRY_LIMIT   20
#define PAGE_SIZE          4096
#define TEST_START         0x00100000
#define MEMTEST_STRIDE     65536 // Sample 1 page every 64 KB (16x speedup)
#define MEMTEST_MAX_PAGES  2048  // Cap test to at most 2048 pages (~128MB sampled, finishes in < 0.2s)

typedef struct __attribute__((packed)) {
    unsigned int base_low;
    unsigned int base_high;
    unsigned int length_low;
    unsigned int length_high;
    unsigned int type;
    unsigned int attributes;
} e820_entry_t;

static unsigned int page_backup[PAGE_SIZE / sizeof(unsigned int)];

static int memtest_page(volatile unsigned int* page) {
    static const unsigned int patterns[] = {
        0xAAAAAAAA, 0x55555555
    };
    unsigned int count = PAGE_SIZE / sizeof(unsigned int);
    int passed = 1;

    for (unsigned int i = 0; i < count; i++) page_backup[i] = page[i];
    for (unsigned int pass = 0; pass < sizeof(patterns) / sizeof(patterns[0]); pass++) {
        for (unsigned int i = 0; i < count; i++) {
            page[i] = patterns[pass];
        }
        for (unsigned int i = 0; i < count; i++) {
            if (page[i] != patterns[pass]) {
                passed = 0;
                break;
            }
        }
        if (!passed) break;
    }
    for (unsigned int i = 0; i < count; i++) page[i] = page_backup[i];
    return passed;
}

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
    unsigned int tested_pages = 0;
    unsigned int errors = 0;

    if (count > E820_ENTRY_LIMIT) count = E820_ENTRY_LIMIT;
    if (count == 0) {
        print_color_string("[MEM] BIOS memory map unavailable; test skipped.\n", VGA_RED);
        return;
    }

    print_string("[MEM] Testing BIOS-reported usable RAM (preserving contents)...\n");
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

        if (start < TEST_START) start = TEST_START;
        if (start > 0xFFFFFFFF - (PAGE_SIZE - 1)) continue;
        start = (start + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        end &= ~(PAGE_SIZE - 1);
        for (unsigned int address = start; address < end; address += MEMTEST_STRIDE) {
            if (tested_pages >= MEMTEST_MAX_PAGES) break;
            if (!memtest_page((volatile unsigned int*)address)) errors++;
            tested_pages++;
        }
    }

    print_string("[MEM] Usable RAM below 4 GiB: ");
    print_unsigned(usable_mb);
    print_string(" MiB; tested pages: ");
    print_unsigned(tested_pages);
    print_string("; failed pages: ");
    print_unsigned(errors);
    print_string(errors ? " (FAIL)\n" : " (PASS)\n");
}

void kernel_main(void) {
    clear_screen();
    enable_hardware_cursor();

    // 1. Initialize CPU Interrupt Descriptor Table (Catches exceptions, prevents Triple Faults)
    idt_init();

    char msg_init[] = "CykOS Kernel Init (IDT + Heap + Syscalls [OK])\n";
    char msg_pass[] = "PASS\n";
    char msg_fail[] = "FAIL\n";

    run_memory_test();
    heap_init();
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
    if (vfs_mount_root() == 0) {
        print_string("VFS mounted root with ");
        print_string(vfs_current_driver());
        print_string(".\n");

        print_string("[Level 1] Verifying File Read [/USR/SHARE/DOC/README.TXT]...\n");
        vfs_file_t readme_file;
        if (vfs_find_file("/USR/SHARE/DOC/README.TXT", &readme_file) == 0 ||
            vfs_find_file("/CYKOS/README.TXT", &readme_file) == 0) {
            char file_buf[128];
            int read_bytes = vfs_read_at(&readme_file, 0, file_buf, sizeof(file_buf) - 1);
            if (read_bytes > 0) {
                file_buf[read_bytes] = '\0';
                print_color_string(" -> Content: ", VGA_GREEN);
                print_string(file_buf);
            } else {
                print_color_string(" -> Read failed!\n", VGA_RED);
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

    print_string("\n[Level 3] Boot sequence complete. Transitioning to CykShell...\n");
    print_string("[Level 3] Flushing boot log to /VAR/LOG/LOG.TXT...\n");
    bootlog_flush("/VAR/LOG/LOG.TXT");
    bootlog_disable();
    secwait(2); 

    // Prefer the standalone Level 3 image; retain the kernel-linked shell fallback.
    exec_shell_binary();
    shell_main();
    
    while (1) {
        asm("hlt");
    }
}
