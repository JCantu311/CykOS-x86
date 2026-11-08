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
    
    // Force the compiler to embed characters directly on the stack as numbers
    char msg_init[] = {'C','y','k','O','S',' ','K','e','r','n','e','l',' ','I','n','i','t','\n','\0'};
    char msg_diag[] = {'R','u','n','n','i','n','g',' ','M','e','m','t','e','s','t','.','.','.','\n','\0'};
    char msg_pass[] = {'P','A','S','S','\n','\0'};
    char msg_fail[] = {'F','A','I','L','\n','\0'};

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
    
    print_color_string(welcome_message, VGA_GREEN);
    
    while (1) {
        asm("hlt");
    }
}
