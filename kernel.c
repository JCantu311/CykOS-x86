// kernel.c
#include "cykastd.h"

// Re-implement your 32-bit memory test function in clean C
int memtest_dword(volatile unsigned int* ptr, unsigned int count) {
    unsigned int pattern = 0xAA55AA55;
    
    // Fill Phase
    for (unsigned int i = 0; i < count; i++) {
        ptr[i] = pattern;
    }
    
    // Verify Phase
    for (unsigned int i = 0; i < count; i++) {
        if (ptr[i] != pattern) {
            return 0; // Fail
        }
    }
    
    return 1; // Pass
}

void kernel_main(void) {
    // 1. Initialize screen and clear BIOS junk text using cykastd
    clear_screen();
    
    print_string("CykaOS Kernel Stage 2 Init... DONE\n");
    print_string("Running memory diagnostic verification checks...\n\n");
    
    // 2. Run memory test at safe location (0x10000)
    unsigned int* test_address = (unsigned int*)0x10000;
    
    print_string("Diagnostic Test Status: ");
    if (memtest_dword(test_address, 1024)) {
        print_color_string("PASS\n", VGA_GREEN);
    } else {
        print_color_string("FAIL\n", VGA_RED);
    }

    print_string("----------------------------------------\n");
    print_string("Ready for input.\n");
    
    while (1) {
        asm("hlt");
    }
}
