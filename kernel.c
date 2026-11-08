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

void kernel_main(void) {
    clear_screen();
    
    // You can now use standard C string literals!
    print_string("CykaOS Kernel Init\n");
    print_string("Running Memtest...\n");
    
    unsigned int* test_address = (unsigned int*)0x10000;
    
    if (memtest_dword(test_address, 1024)) {
        print_color_string("PASS\n", VGA_GREEN);
    } else {
        print_color_string("FAIL\n", VGA_RED);
    }
    
    while (1) {
        asm("hlt");
    }
}
