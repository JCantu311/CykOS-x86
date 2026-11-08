#include "cykastd.h"

// Tracking variables scoped internally to this module
static int cursor_x = 0;
static int cursor_y = 0;
static const unsigned char DEFAULT_COLOR = 0x0F; // White text on black background

void clear_screen(void) {
    unsigned short* vga = (unsigned short*)VGA_ADDRESS;
    unsigned short blank = (DEFAULT_COLOR << 8) | ' ';
    
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga[i] = blank;
    }
    
    cursor_x = 0;
    cursor_y = 0;
}

static void scroll_screen(void) {
    unsigned short* vga = (unsigned short*)VGA_ADDRESS;
    unsigned short blank = (DEFAULT_COLOR << 8) | ' ';

    // Move rows 1-24 up to rows 0-23
    for (int y = 1; y < VGA_HEIGHT; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            vga[(y - 1) * VGA_WIDTH + x] = vga[y * VGA_WIDTH + x];
        }
    }

    // Clear the very bottom row (row 24)
    for (int x = 0; x < VGA_WIDTH; x++) {
        vga[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = blank;
    }

    cursor_y = VGA_HEIGHT - 1;
}

void print_char(char c) {
    unsigned short* vga = (unsigned short*)VGA_ADDRESS;

    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else {
        int index = (cursor_y * VGA_WIDTH) + cursor_x;
        vga[index] = (DEFAULT_COLOR << 8) | c;
        cursor_x++;
    }

    if (cursor_x >= VGA_WIDTH) {
        cursor_x = 0;
        cursor_y++;
    }

    if (cursor_y >= VGA_HEIGHT) {
        scroll_screen();
    }
}

void print_string(const char* str) {
    for (int i = 0; str[i] != '\0'; i++) {
        print_char(str[i]);
    }
}

int strncmp(const char* str1, const char* str2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (str1[i] != str2[i]) {
            return (unsigned char)str1[i] - (unsigned char)str2[i];
        }
        if (str1[i] == '\0') {
            return 0;
        }
    }
    return 0;
}

void print_color_string(const char* str, unsigned char color) {
    unsigned int full_char = 0x0000;
    full_char = color * 0x100;
    for(int i = 0; str[i] != '\0'; i++) {
        unsigned short* vga = (unsigned short*)VGA_ADDRESS;
        int index = (cursor_y * VGA_WIDTH) + cursor_x;
        vga[index] = full_char | str[i];
        cursor_x++;

        if (cursor_x >= VGA_WIDTH) {
            cursor_x = 0;
            cursor_y++;
        }

        if (cursor_y >= VGA_HEIGHT) {
            scroll_screen();
        }
    }
}