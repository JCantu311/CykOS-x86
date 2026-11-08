#ifndef CYKASTD_H
#define CYKASTD_H

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_ADDRESS 0xB8000
typedef unsigned int size_t;

// Public function declarations
void clear_screen(void);
void print_char(char c);
void print_string(const char* str);
int memtest_dword(unsigned int* ptr, unsigned int count);
int strncmp(const char* str1, const char* str2, size_t n);
void print_color_string(const char* str, unsigned char color);

#endif // CYKASTD_H
