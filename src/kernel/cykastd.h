#ifndef CYKASTD_H
#define CYKASTD_H

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_ADDRESS 0xB8000

#define VGA_WHITE 0x0F
#define VGA_GREEN 0x0A
#define VGA_RED   0x0C
#define VGA_BLUE  0x09
#define VGA_YELLOW 0x0E

typedef unsigned int size_t;

// Public Standard I/O Function Declarations
void clear_screen(void);
void uefi_console_init(void);
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
void get_cursor_pos(int* x, int* y);
void set_cursor_pos(int x, int y);

// NEW: Hardware Port Communication Wrappers
unsigned char inb(unsigned short port);
void outb(unsigned short port, unsigned char data);
unsigned short inw(unsigned short port);
void outw(unsigned short port, unsigned short data);
unsigned int inl(unsigned short port);
void outl(unsigned short port, unsigned int data);

void secwait(unsigned int seconds);
void msecwait(unsigned int milliseconds);

// Navigation & Special Key Codes (0x80 - 0x89)
#define KEY_UP       ((unsigned char)0x80)
#define KEY_DOWN     ((unsigned char)0x81)
#define KEY_LEFT     ((unsigned char)0x82)
#define KEY_RIGHT    ((unsigned char)0x83)
#define KEY_HOME     ((unsigned char)0x84)
#define KEY_END      ((unsigned char)0x85)
#define KEY_PAGEUP   ((unsigned char)0x86)
#define KEY_PAGEDOWN ((unsigned char)0x87)
#define KEY_INSERT   ((unsigned char)0x88)
#define KEY_DELETE   ((unsigned char)0x89)

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

// Boot Logging System
void bootlog_append_char(char c);
void bootlog_append_string(const char* str);
int bootlog_flush(const char* filepath);
void bootlog_disable(void);

// POSIX-standard keyboard driver interface.
// These functions delegate to the Level 2 keyboard driver loaded via modload.
// If no keyboard driver is present (not loaded from MODULES.CFG alongside
// DRIVERS.LST), the behavior is undefined - direct hardware access may be
// required for bare-metal operation, but application code targeting this
// API must ensure a compliant driver is loaded first.

int has_key(void);
char get_scancode(void);
char scancode_to_ascii(unsigned char scancode);
char get_char(void);

/* POSIX-standard sound driver interface.
// Function signatures standardized for porting software (Doom, SDL, etc.)
// that expect these exact definitions.

void tone(unsigned int frequency, unsigned int milliseconds);
void chime(int play_sample(const unsigned char* data, unsigned int len, unsigned int freq_hz);
int set_volume(unsigned int volume);
int stop(void);
int get_caps(unsigned char hardware_name[32]);
*/

/* POSIX-standard display/graphics driver interface.
// Standardized function signatures for porting graphics software.

int set_mode(int mode);
int get_mode(void);
void put_pixel(int x, int y, unsigned char color);
void fill_rect(int x, int y, int w, int h, unsigned char color);
void clear(unsigned char color);
void draw_bitmap(int x, int y, int w, int h, const unsigned char* data);
unsigned char* get_framebuffer(void);
*/

#endif // CYKASTD_H
