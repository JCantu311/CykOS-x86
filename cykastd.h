#ifndef CYKASTD_H
#define CYKASTD_H

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_ADDRESS 0xB8000

#define VGA_RED 0x4
#define VGA_GREEN 0x2
#define VGA_BLUE 0x1
#define VGA_WHITE 0x7

typedef unsigned int size_t;

// Public Standard I/O Function Declarations
void clear_screen(void);
void print_char(char c);
void print_string(const char* str);
int strncmp(const char* str1, const char* str2, size_t n);
void print_color_string(const char* str, unsigned char color);

#endif // CYKASTD_H
