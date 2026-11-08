// kernel.c
#include "cykastd.h"

void kernel_main(void) {
    unsigned short* vga_buffer = (unsigned short*)0xB8000;
    
    // 1. Clear Screen (80 columns * 25 rows = 2000 characters)
    for (int i = 0; i < 80 * 25; i++) {
        vga_buffer[i] = 0x0F20; // Black background, space char
    }
    
    // 2. Run your memory test at a safe location (0x10000)
    unsigned int* test_address = (unsigned int*)0x10000;
    
    if (memtest_dword(test_address, 1024)) {
        // Print "PASS" in Light Green (0x0A) text
        vga_buffer[0] = 0x0A50; // 'P'
        vga_buffer[1] = 0x0A41; // 'A'
        vga_buffer[2] = 0x0A53; // 'S'
        vga_buffer[3] = 0x0A53; // 'S'
    } else {
        // Print "FAIL" in Bright Red (0x0C) text
        vga_buffer[0] = 0x0C46; // 'F'
        vga_buffer[1] = 0x0C41; // 'A'
        vga_buffer[2] = 0x0C49; // 'I'
        vga_buffer[3] = 0x0C4C; // 'L'
    }


    
    while (1) {
        asm("hlt");
    }
}
