// kernel.c
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
    
    // Standard string literals are fully operational via runtime offset tracking!
    print_string(fix_ptr("CykaOS Kernel Stage 2 Init... DONE\n"));
    print_string(fix_ptr("Running memory diagnostic verification checks...\n\n"));
    
    unsigned int* test_address = (unsigned int*)0x10000;
    
    print_string(fix_ptr("Diagnostic Test Status: "));
    if (memtest_dword(test_address, 1024)) {
        print_color_string(fix_ptr("PASS\n"), VGA_GREEN);
    } else {
        print_color_string(fix_ptr("FAIL\n"), VGA_RED);
    }

    print_string(fix_ptr("----------------------------------------\n"));
    print_string(fix_ptr("Ready for input.\n"));
    
    while (1) {
        asm("hlt");
    }
}
