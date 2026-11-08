#include "cykastd.h"

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

int disk_test(void) {
    // TODO: Disk test implementation
    return 1;
}

void kernel_main(void) {
    clear_screen();
    
    // Force the compiler to embed characters directly on the stack as numbers
    char msg_init[] = "CykOS Kernel Init\n";
    char msg_diag[] = "Running Memtest...\n";
    char msg_pass[] = "PASS\n";
    char msg_fail[] = "FAIL\n";

    print_string(msg_init);
    print_string(msg_diag);
    
    unsigned int* test_address = (unsigned int*)0x10000;
    
    if (memtest_dword(test_address, 1024)) {
        print_color_string(msg_pass, VGA_GREEN);
    } else {
        print_color_string(msg_fail, VGA_RED);
    }

    char cykaos_version[] = "CykOS v0.0.1\n";

    print_string(cykaos_version);

    char welcome_message[] = "Welcome to CykOS...\n";

    char disk_test[] = "Running Disk Test...\n";

    if(disk_test == 1) {
        print_color_string(msg_fail, VGA_RED);
    } else if(disk_test == 0) {
        print_color_string(msg_pass, VGA_GREEN);
    }

    print_color_string(welcome_message, VGA_GREEN);
    
    while (1) {
        asm("hlt");
    }
}
