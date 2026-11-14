#include "cykastd.h"
#include "modload.h"
#include "vfs.h"

#define CONSOLE_COLOR VGA_WHITE

#define BOOT_LOG_CAPACITY 2048
static char g_boot_log_buf[BOOT_LOG_CAPACITY];
static unsigned int g_boot_log_len = 0;
static int g_boot_log_enabled = 1;

void bootlog_append_char(char c) {
    if (!g_boot_log_enabled) return;
    if (g_boot_log_len + 1 < BOOT_LOG_CAPACITY) {
        g_boot_log_buf[g_boot_log_len++] = c;
    }
}

void bootlog_append_string(const char* str) {
    if (!str || !g_boot_log_enabled) return;
    while (*str) {
        bootlog_append_char(*str++);
    }
}

void bootlog_disable(void) {
    g_boot_log_enabled = 0;
}

void bootlog_flush(const char* filepath) {
    if (!filepath || g_boot_log_len == 0) return;
    g_boot_log_buf[g_boot_log_len] = '\0';
    vfs_write_file(filepath, g_boot_log_buf, g_boot_log_len);
}

static int cursor_x = 0;
static int cursor_y = 0;

void update_hardware_cursor(void) {
    unsigned short pos = (cursor_y * VGA_WIDTH) + cursor_x;

    outb(0x3D4, 0x0F);
    outb(0x3D5, (unsigned char)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (unsigned char)((pos >> 8) & 0xFF));
}

void enable_hardware_cursor(void) {
    // Enable standard underline cursor (scanlines 14 to 15 in 16-line font)
    outb(0x3D4, 0x0A);
    outb(0x3D5, (inb(0x3D5) & 0xC0) | 14);

    outb(0x3D4, 0x0B);
    outb(0x3D5, (inb(0x3D5) & 0xE0) | 15);

    update_hardware_cursor();
}

void get_cursor_pos(int* x, int* y) {
    if (x) *x = cursor_x;
    if (y) *y = cursor_y;
}

void set_cursor_pos(int x, int y) {
    if (x < 0) x = 0;
    if (x >= VGA_WIDTH) x = VGA_WIDTH - 1;
    if (y < 0) y = 0;
    if (y >= VGA_HEIGHT) y = VGA_HEIGHT - 1;
    cursor_x = x;
    cursor_y = y;
    update_hardware_cursor();
}

void clear_screen(void) {
    unsigned short* vga = (unsigned short*)VGA_ADDRESS;
    unsigned short blank = (CONSOLE_COLOR << 8) | ' ';
    
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga[i] = blank;
    }
    
    cursor_x = 0;
    cursor_y = 0;
    update_hardware_cursor();
}

static void scroll_screen(void) {
    unsigned short* vga = (unsigned short*)VGA_ADDRESS;
    unsigned short blank = (CONSOLE_COLOR << 8) | ' ';

    // Move rows 1-24 up to rows 0-23
    for (int y = 1; y < VGA_HEIGHT; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            vga[(y - 1) * VGA_WIDTH + x] = vga[y * VGA_WIDTH + x];
        }
    }

    // Clear the bottom boundary row
    for (int x = 0; x < VGA_WIDTH; x++) {
        vga[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = blank;
    }

    cursor_y = VGA_HEIGHT - 1;
    update_hardware_cursor();
}

void print_char(char c) {
    bootlog_append_char(c);
    unsigned short* vga = (unsigned short*)VGA_ADDRESS;

    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\b') {
        if (cursor_x > 0) {
            cursor_x--;
            int index = (cursor_y * VGA_WIDTH) + cursor_x;
            vga[index] = (CONSOLE_COLOR << 8) | ' ';
        }
    } else {
        int index = (cursor_y * VGA_WIDTH) + cursor_x;
        vga[index] = (CONSOLE_COLOR << 8) | (unsigned char)c;
        cursor_x++;
    }

    if (cursor_x >= VGA_WIDTH) {
        cursor_x = 0;
        cursor_y++;
    }

    if (cursor_y >= VGA_HEIGHT) {
        scroll_screen();
    }

    update_hardware_cursor();
}

void print_string(const char* str) {
    if (!str) return;
    for (int i = 0; str[i] != '\0'; i++) {
        print_char(str[i]);
    }
}

void print_color_string(const char* str, unsigned char txt_color) {
    if (!str) return;
    bootlog_append_string(str);
    unsigned short* vga = (unsigned short*)VGA_ADDRESS;
    unsigned short full_char = (unsigned short)txt_color << 8;
    const char* p = str;

    while (*p != '\0') {
        if (*p == '\n') {
            cursor_x = 0;
            cursor_y++;
        } else {
            int index = (cursor_y * VGA_WIDTH) + cursor_x;
            vga[index] = full_char | (unsigned char)*p;
            cursor_x++;
        }

        if (cursor_x >= VGA_WIDTH) {
            cursor_x = 0;
            cursor_y++;
        }

        if (cursor_y >= VGA_HEIGHT) {
            scroll_screen();
        }
        p++;
    }

    update_hardware_cursor();
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

size_t strlen(const char* str) {
    size_t len = 0;
    while (str && str[len] != '\0') {
        len++;
    }
    return len;
}

void* memcpy(void* dest, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dest;
}

void* memset(void* dest, int val, size_t n) {
    unsigned char* d = (unsigned char*)dest;
    for (size_t i = 0; i < n; i++) {
        d[i] = (unsigned char)val;
    }
    return dest;
}

void* memmove(void* dest, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    if (d < s) {
        for (size_t i = 0; i < n; i++) {
            d[i] = s[i];
        }
    } else if (d > s) {
        for (size_t i = n; i > 0; i--) {
            d[i - 1] = s[i - 1];
        }
    }
    return dest;
}

int memcmp(const void* s1, const void* s2, size_t n) {
    const unsigned char* p1 = (const unsigned char*)s1;
    const unsigned char* p2 = (const unsigned char*)s2;
    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) {
            return (int)p1[i] - (int)p2[i];
        }
    }
    return 0;
}

int bcmp(const void* s1, const void* s2, size_t n) {
    return memcmp(s1, s2, n);
}

void rust_eh_personality(void) {
}

void rust_begin_unwind(void) {
    while (1) {
        asm volatile("hlt");
    }
}

void msecwait(unsigned int milliseconds) {
    if (milliseconds == 0) return;
    // Calibrated delay:
    // Each inb(0x80) on modern QEMU takes ~0.1 - 0.2 microseconds,
    // and compiler loop overhead is minimal.
    // 5,000 inb(0x80) reads reliably approximates 1-2 milliseconds across hypervisors
    // without resetting or stopping PIT Channel 2 on Port 0x61.
    for (unsigned int i = 0; i < milliseconds; i++) {
        for (unsigned int j = 0; j < 5000; j++) {
            asm volatile("inb $0x80, %%al" : : : "eax");
        }
    }
}

void secwait(unsigned int seconds) {
    msecwait(seconds * 1000);
}

unsigned char inb(unsigned short port) {
    unsigned char result;
    asm volatile("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

void outb(unsigned short port, unsigned char data) {
    asm volatile("outb %0, %1" :: "a"(data), "Nd"(port));
}

// Read a 16-bit word from an I/O port (vital for ATA PIO data transfers)
unsigned short inw(unsigned short port) {
    unsigned short result;
    asm volatile("inw %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

// Write a 16-bit word to an I/O port
void outw(unsigned short port, unsigned short data) {
    asm volatile("outw %0, %1" :: "a"(data), "Nd"(port));
}

unsigned int inl(unsigned short port) {
    unsigned int result;
    asm volatile("inl %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

void outl(unsigned short port, unsigned int data) {
    asm volatile("outl %0, %1" :: "a"(data), "Nd"(port));
}

// Keyboard Hardware Interface Functions (Delegates to loaded Level 2 Keyboard Driver)
int has_key(void) {
    cyk_keyboard_driver_t* kbd = modload_get_keyboard_driver();
    if (kbd && kbd->has_key) {
        return kbd->has_key();
    }
    return (inb(0x64) & 0x01) != 0;
}

char get_scancode(void) {
    cyk_keyboard_driver_t* kbd = modload_get_keyboard_driver();
    if (kbd && kbd->get_scancode) {
        return kbd->get_scancode();
    }
    while (!(inb(0x64) & 0x01)) {
        asm volatile("pause");
    }
    return (char)inb(0x60);
}

// US QWERTY Set 1 fallback scancode table
static const char fallback_scancode_normal[128] = {
    0,   27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,   'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,   '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0,   ' ', 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
};

static int g_fallback_extended = 0;
static int g_fallback_ctrl = 0;

char scancode_to_ascii(unsigned char scancode) {
    cyk_keyboard_driver_t* kbd = modload_get_keyboard_driver();
    if (kbd && kbd->scancode_to_ascii) {
        return kbd->scancode_to_ascii(scancode);
    }
    if (scancode == 0xE0) {
        g_fallback_extended = 1;
        return 0;
    }
    if (scancode == 0xE1) {
        return 0;
    }
    if (scancode & 0x80) {
        unsigned char released = scancode & 0x7F;
        if (released == 0x1D) {
            g_fallback_ctrl = 0;
        }
        g_fallback_extended = 0;
        return 0; // Key release
    }
    g_fallback_extended = 0;
    if (scancode == 0x1D) {
        g_fallback_ctrl = 1;
        return 0;
    }
    switch (scancode) {
        case 0x48: return KEY_UP;
        case 0x50: return KEY_DOWN;
        case 0x4B: return KEY_LEFT;
        case 0x4D: return KEY_RIGHT;
        case 0x47: return KEY_HOME;
        case 0x4F: return KEY_END;
        case 0x49: return KEY_PAGEUP;
        case 0x51: return KEY_PAGEDOWN;
        case 0x52: return KEY_INSERT;
        case 0x53: return KEY_DELETE;
        default: break;
    }
    if (scancode < 128) {
        if (g_fallback_ctrl && scancode == 0x2E) {
            return 0x03; // Ctrl+C
        }
        return fallback_scancode_normal[scancode];
    }
    return 0;
}

char get_char(void) {
    cyk_keyboard_driver_t* kbd = modload_get_keyboard_driver();
    if (kbd && kbd->get_char) {
        return kbd->get_char();
    }
    while (1) {
        char sc = get_scancode();
        char ascii = scancode_to_ascii((unsigned char)sc);
        if (ascii != 0) {
            return ascii;
        }
    }
}