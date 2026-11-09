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
void* memmove(void* dest, const void* src, size_t n);
int memcmp(const void* s1, const void* s2, size_t n);
int bcmp(const void* s1, const void* s2, size_t n);
void rust_eh_personality(void);
void print_color_string(const char* str, unsigned char color);
void update_hardware_cursor(void);
void enable_hardware_cursor(void);

// NEW: Hardware Port Communication Wrappers
unsigned char inb(unsigned short port);
void outb(unsigned short port, unsigned char data);
unsigned short inw(unsigned short port);
void outw(unsigned short port, unsigned short data);

void secwait(unsigned int seconds);

// Navigation & Special Key Codes (0x80 - 0x89)
#define KEY_UP       ((char)0x80)
#define KEY_DOWN     ((char)0x81)
#define KEY_LEFT     ((char)0x82)
#define KEY_RIGHT    ((char)0x83)
#define KEY_HOME     ((char)0x84)
#define KEY_END      ((char)0x85)
#define KEY_PAGEUP   ((char)0x86)
#define KEY_PAGEDOWN ((char)0x87)
#define KEY_INSERT   ((char)0x88)
#define KEY_DELETE   ((char)0x89)

// NEW: Keyboard Engine
char get_scancode(void);
char scancode_to_ascii(unsigned char scancode);
char get_char(void);
int has_key(void);

// Dynamic Memory Heap Allocator
void* malloc(size_t size);
void  free(void* ptr);
void* realloc(void* ptr, size_t size);
void* calloc(size_t num, size_t size);

#endif // CYKASTD_H
