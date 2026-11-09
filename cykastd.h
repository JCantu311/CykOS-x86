#ifndef CYKASTD_H
#define CYKASTD_H

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_ADDRESS 0xB8000

#define VGA_WHITE 0x0F
#define VGA_GREEN 0x0A
#define VGA_RED   0x0C
#define VGA_BLUE  0x09

typedef unsigned int size_t;

// Public Standard I/O Function Declarations
void clear_screen(void);
void print_char(char c);
void print_string(const char* str);
int strncmp(const char* str1, const char* str2, size_t n);
size_t strlen(const char* str);
void* memcpy(void* dest, const void* src, size_t n);
void* memset(void* dest, int val, size_t n);
void print_color_string(const char* str, unsigned char color);

// NEW: Hardware Port Communication Wrappers
unsigned char inb(unsigned short port);
void outb(unsigned short port, unsigned char data);
unsigned short inw(unsigned short port);
void outw(unsigned short port, unsigned short data);

void secwait(unsigned int seconds);

// NEW: Keyboard Engine
char get_scancode(void);
char scancode_to_ascii(unsigned char scancode);

#endif // CYKASTD_H
