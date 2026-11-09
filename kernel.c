#include "cykastd.h"
#include "ata.h"
#include "fat16.h"
#include "modload.h"

// Level 3 CykShell Entry Point (implemented in shell.rs)
extern void shell_main(void);

int memtest_dword(volatile unsigned int* ptr, unsigned int count) {
    unsigned int pattern = 0xAA55AA55;
    for (unsigned int i = 0; i < count; i++) {
        ptr[i] = pattern;
    }
    for (unsigned int i = 0; i < count; i++) {
        if (ptr[i] != pattern) {
            return 0; 
        }
    }
    return 1; 
}

void kernel_main(void) {
    clear_screen();
    enable_hardware_cursor();
    
    char msg_init[] = "CykOS Kernel Init\n";
    char msg_diag[] = "Running Memtest...\n";
    char msg_pass[] = "PASS\n";
    char msg_fail[] = "FAIL\n";

    print_string(msg_init);
    print_string(msg_diag);
    
    // Memtest a safe conventional RAM location (0x60000) that does not collide with the kernel
    unsigned int* test_address = (unsigned int*)0x60000;
    
    if (memtest_dword(test_address, 1024)) {
        print_color_string(msg_pass, VGA_GREEN);
    } else {
        print_color_string(msg_fail, VGA_RED);
    }

    char cykaos_version[] = "CykOS v0.0.1\n";
    print_string(cykaos_version);

    char welcome_message[] = "Welcome to CykOS...\n";
    char disk_test_msg[] = "Running Disk Test...\n";

    print_string(disk_test_msg);

    int disk_result = disk_test();

    if (disk_result == 0) {
        print_color_string(msg_pass, VGA_GREEN);
    } else {
        print_color_string(msg_fail, VGA_RED);
    }

    print_color_string(welcome_message, VGA_GREEN);

    // Initialize Level 1 FAT16 Filesystem driver
    print_string("\n[Level 1] Initializing FAT16 Filesystem...\n");
    if (fat16_init() == 0) {
        fat16_print_volume_info();

        print_string("[Level 1] Verifying File Read [README.TXT]...\n");
        fat16_entry_t readme_file;
        if (fat16_find_file("README.TXT", &readme_file) == 0) {
            char file_buf[128];
            int read_bytes = fat16_read_file(&readme_file, file_buf, sizeof(file_buf) - 1);
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
        print_color_string("FAT16: No filesystem detected on disk.\n", VGA_RED);
    }

    // Load Level 2 driver modules from MODULES.CFG
    print_string("\n[Level 2] Loading Driver Modules from MODULES.CFG...\n");
    int modules_loaded = modload_load_all();
    if (modules_loaded > 0) {
        print_color_string("[Level 2] Driver Modules Loaded Successfully.\n", VGA_GREEN);
    } else {
        print_color_string("[Level 2] Warning: No Level 2 modules loaded.\n", VGA_RED);
    }

    print_string("\n[Level 3] Boot sequence complete. Transitioning to CykShell...\n");
    // secwait(2); 

    // Launch Level 3 CykShell directly without waiting for user input
    shell_main();
    
    while (1) {
        asm("hlt");
    }
}
