#include "cykastd.h"
#include "modload.h"
#include "vfs.h"
#include "usb.h"
#include "paging.h"

/* Error codes for modular keyboard operations */
#define KEYBOARD_ERROR (-1)

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

int bootlog_flush(const char* filepath) {
    if (!filepath || g_boot_log_len == 0) return -1;
    g_boot_log_buf[g_boot_log_len] = '\0';
    return vfs_write_file(filepath, g_boot_log_buf, g_boot_log_len);
}

static int cursor_x = 0;
static int cursor_y = 0;
static volatile unsigned int* g_uefi_framebuffer;
static unsigned int g_uefi_framebuffer_pitch;
static unsigned int g_uefi_framebuffer_width;
static unsigned int g_uefi_framebuffer_height;
/* The BIOS image has no Multiboot2 handoff object. Keep a zero default there;
 * the UEFI entry object provides the strong definition used by its image. */
__attribute__((weak)) unsigned int multiboot_info_pointer = 0;

static const unsigned char g_font_upper[26][5] = {
    {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},
    {0x3E,0x41,0x41,0x41,0x22},{0x7F,0x41,0x41,0x22,0x1C},
    {0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},
    {0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},
    {0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},
    {0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},
    {0x3E,0x41,0x41,0x41,0x3E},{0x7F,0x09,0x09,0x09,0x06},
    {0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},
    {0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},
    {0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43}
};
static const unsigned char g_font_digits[10][5] = {
    {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},
    {0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E}
};

static unsigned int console_rgb(unsigned char color) {
    static const unsigned int palette[16] = {
        0x000000,0x0000AA,0x00AA00,0x00AAAA,0xAA0000,0xAA00AA,0xAA5500,0xAAAAAA,
        0x555555,0x5555FF,0x55FF55,0x55FFFF,0xFF5555,0xFF55FF,0xFFFF55,0xFFFFFF
    };
    return palette[color & 0x0Fu];
}

void uefi_console_init(void) {
    if (*(volatile unsigned int*)0x04ECu != 0x49464555u) return;
    unsigned int info_address = multiboot_info_pointer;
    if (!info_address) return;
    const unsigned int* info = (const unsigned int*)info_address;
    unsigned int total_size = info[0];
    if (total_size < 16u || info_address > 0xFFFFFFFFu - total_size) return;
    unsigned int cursor = info_address + 8u;
    unsigned int end = info_address + total_size;
    while (cursor <= end - 8u) {
        const unsigned int* tag = (const unsigned int*)cursor;
        unsigned int type = tag[0], size = tag[1];
        if (size < 8u || size > end - cursor) return;
        if (type == 0) break;
        if (type == 8 && size >= 32u) {
            const unsigned char* bytes = (const unsigned char*)tag;
            unsigned int address = *(const unsigned int*)(bytes + 8);
            unsigned int address_high = *(const unsigned int*)(bytes + 12);
            unsigned int pitch = *(const unsigned int*)(bytes + 16);
            unsigned int width = *(const unsigned int*)(bytes + 20);
            unsigned int height = *(const unsigned int*)(bytes + 24);
            unsigned int depth = bytes[28];
            if (address_high || !address || depth != 32 || width < VGA_WIDTH * 8u ||
                height < VGA_HEIGHT * 16u || pitch < width * 4u ||
                height > 0xFFFFFFFFu / pitch) return;
            unsigned int length = pitch * height;
            if (address > 0xFFFFFFFFu - length ||
                paging_map_mmio_range(address, length) != 0) return;
            g_uefi_framebuffer = (volatile unsigned int*)address;
            g_uefi_framebuffer_pitch = pitch;
            g_uefi_framebuffer_width = width;
            g_uefi_framebuffer_height = height;
            return;
        }
        cursor += (size + 7u) & ~7u;
    }
}

static unsigned int framebuffer_text_x(void) {
    return (g_uefi_framebuffer_width - VGA_WIDTH * 8u) / 2u;
}

static unsigned int framebuffer_text_y(void) {
    return (g_uefi_framebuffer_height - VGA_HEIGHT * 16u) / 2u;
}

static const unsigned char* framebuffer_glyph(unsigned char ch) {
    static const unsigned char blank[5] = {0,0,0,0,0};
    static const unsigned char question[5] = {0x02,0x01,0x51,0x09,0x06};
    static const unsigned char punctuation[16][5] = {
        {0,0,0x5F,0,0}, {0,0x07,0,0x07,0}, {0x14,0x7F,0x14,0x7F,0x14},
        {0x24,0x2A,0x7F,0x2A,0x12}, {0x23,0x13,0x08,0x64,0x62},
        {0x36,0x49,0x55,0x22,0x50}, {0,0x05,0x03,0,0},
        {0,0x1C,0x22,0x41,0}, {0,0x41,0x22,0x1C,0},
        {0x14,0x08,0x3E,0x08,0x14}, {0x08,0x08,0x3E,0x08,0x08},
        {0,0x50,0x30,0,0}, {0x08,0x08,0x08,0x08,0x08},
        {0,0x60,0x60,0,0}, {0x20,0x10,0x08,0x04,0x02},
        {0x02,0x04,0x08,0x10,0x20}
    };
    if (ch >= 'a' && ch <= 'z') ch = (unsigned char)(ch - 'a' + 'A');
    if (ch >= 'A' && ch <= 'Z') return g_font_upper[ch - 'A'];
    if (ch >= '0' && ch <= '9') return g_font_digits[ch - '0'];
    if (ch == ' ') return blank;
    switch (ch) {
        case '!': return punctuation[0]; case '"': return punctuation[1];
        case '#': return punctuation[2]; case '$': return punctuation[3];
        case '%': return punctuation[4]; case '&': return punctuation[5];
        case '\'': return punctuation[6]; case '(': return punctuation[7];
        case ')': return punctuation[8]; case '*': return punctuation[9];
        case '+': return punctuation[10]; case ',': return punctuation[11];
        case '-': return punctuation[12]; case '.': return punctuation[13];
        case '/': return punctuation[14]; case '\\': return punctuation[15];
        case ':': return question; case ';': return question; case '<': return question;
        case '=': return question; case '>': return question; case '?': return question;
        case '[': return question; case ']': return question; case '_': return question;
        case '|': return question; default: return question;
    }
}

static void framebuffer_draw_char(char character, unsigned char color) {
    if (!g_uefi_framebuffer) return;
    unsigned int left = framebuffer_text_x() + (unsigned int)cursor_x * 8u;
    unsigned int top = framebuffer_text_y() + (unsigned int)cursor_y * 16u;
    const unsigned char* glyph = framebuffer_glyph((unsigned char)character);
    unsigned int rgb = console_rgb(color);
    for (unsigned int row = 0; row < 16u; ++row) {
        volatile unsigned int* pixels = (volatile unsigned int*)((unsigned char*)g_uefi_framebuffer +
            (top + row) * g_uefi_framebuffer_pitch) + left;
        unsigned int glyph_row = row / 2u;
        for (unsigned int column = 0; column < 8u; ++column) {
            unsigned int on = column > 0 && column < 6 &&
                (glyph[column - 1u] & (1u << glyph_row));
            pixels[column] = on ? rgb : 0;
        }
    }
}

static void framebuffer_clear(void) {
    if (!g_uefi_framebuffer) return;
    unsigned int pixels_per_row = g_uefi_framebuffer_pitch / 4u;
    for (unsigned int y = 0; y < g_uefi_framebuffer_height; ++y) {
        volatile unsigned int* row = g_uefi_framebuffer + y * pixels_per_row;
        for (unsigned int x = 0; x < pixels_per_row; ++x) row[x] = 0;
    }
}

void update_hardware_cursor(void) {
    if (g_uefi_framebuffer) return;
    unsigned short pos = (cursor_y * VGA_WIDTH) + cursor_x;

    outb(0x3D4, 0x0F);
    outb(0x3D5, (unsigned char)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (unsigned char)((pos >> 8) & 0xFF));
}

void enable_hardware_cursor(void) {
    if (g_uefi_framebuffer) return;
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
    if (g_uefi_framebuffer) {
        framebuffer_clear();
        cursor_x = 0;
        cursor_y = 0;
        return;
    }
    unsigned short* vga = (unsigned short*)VGA_ADDRESS;
    unsigned short blank = (CONSOLE_COLOR << 8) | ' ';
    
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga[i] = blank;
    }
    
    cursor_x = 0;
    cursor_y = 0;
    update_hardware_cursor();
}

// Restore the VGA text mode and its saved DAC/font state before clearing.
// The shell uses this after graphics applications that may have changed the
// VGA palette while leaving the driver in an inconsistent mode state.
void console_clear(void) {
    if (g_uefi_framebuffer) {
        clear_screen();
        return;
    }
    cyk_display_driver_t* display = modload_get_display_driver();
    if (display && display->set_mode) {
        display->set_mode(0);
        return; // set_mode(0) also clears the text screen
    }
    clear_screen();
}

static void scroll_screen(void) {
    if (g_uefi_framebuffer) {
        unsigned int top = framebuffer_text_y();
        unsigned int rows = VGA_HEIGHT * 16u;
        for (unsigned int y = 0; y < rows - 16u; ++y) {
            unsigned char* destination = (unsigned char*)g_uefi_framebuffer +
                (top + y) * g_uefi_framebuffer_pitch;
            unsigned char* source = destination + 16u * g_uefi_framebuffer_pitch;
            memmove(destination, source, g_uefi_framebuffer_pitch);
        }
        unsigned int bottom = top + rows - 16u;
        for (unsigned int y = 0; y < 16u; ++y) {
            volatile unsigned int* pixels = (volatile unsigned int*)((unsigned char*)g_uefi_framebuffer +
                (bottom + y) * g_uefi_framebuffer_pitch);
            for (unsigned int x = 0; x < g_uefi_framebuffer_pitch / 4u; ++x) pixels[x] = 0;
        }
        cursor_y = VGA_HEIGHT - 1;
        return;
    }
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
            if (g_uefi_framebuffer) framebuffer_draw_char(' ', CONSOLE_COLOR);
            else vga[index] = (CONSOLE_COLOR << 8) | ' ';
        }
    } else {
        int index = (cursor_y * VGA_WIDTH) + cursor_x;
        if (g_uefi_framebuffer) framebuffer_draw_char(c, CONSOLE_COLOR);
        else vga[index] = (CONSOLE_COLOR << 8) | (unsigned char)c;
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
            if (g_uefi_framebuffer) framebuffer_draw_char(*p, txt_color);
            else vga[index] = full_char | (unsigned char)*p;
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
    void* result = dest;
    size_t words = n / sizeof(unsigned int);
    size_t tail = n % sizeof(unsigned int);
    asm volatile("cld; rep movsl"
                 : "+D"(dest), "+S"(src), "+c"(words)
                 :
                 : "memory");
    asm volatile("rep movsb"
                 : "+D"(dest), "+S"(src), "+c"(tail)
                 :
                 : "memory");
    return result;
}

void* memset(void* dest, int val, size_t n) {
    void* result = dest;
    unsigned int fill = (unsigned char)val;
    fill |= fill << 8;
    fill |= fill << 16;
    size_t words = n / sizeof(unsigned int);
    size_t tail = n % sizeof(unsigned int);
    asm volatile("cld; rep stosl"
                 : "+D"(dest), "+c"(words)
                 : "a"(fill)
                 : "memory");
    asm volatile("rep stosb"
                 : "+D"(dest), "+c"(tail)
                 : "a"((unsigned char)val)
                 : "memory");
    return result;
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
    // Each inb(0x80) on QEMU/real hardware is fast (~sub-microsecond),
    // and compiler loop overhead is minimal.
    // 5,000 inb(0x80) reads reliably approximates 1-2 milliseconds
    // without touching PIT channels (avoids interfering with channel 2
    // used by the sound driver, and avoids PIT port I/O storms on QEMU TCG).
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
    if (!kbd || !kbd->has_key) return KEYBOARD_ERROR;
    if (kbd->has_key()) return 1;
    usb_poll();
    return kbd->has_key();
}

char get_scancode(void) {
    cyk_keyboard_driver_t* kbd = modload_get_keyboard_driver();
    if (!kbd || !kbd->has_key || !kbd->get_scancode) return KEYBOARD_ERROR;
    while (1) {
        if (kbd->has_key()) return kbd->get_scancode();
        usb_poll();
        if (kbd->has_key()) return kbd->get_scancode();
        asm volatile("pause");
    }
}

/* Legacy... */

// US QWERTY Set 1 fallback scancode table (legacy - modular driver API required)
__attribute__((unused))
static const char fallback_scancode_normal[128] = {
    0,   27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,   'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,   '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0,   ' ', 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
};/* Suppressed: legacy scancode table - modular driver API required */

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

/* Legacy... */
        return 0;
    }
    if (scancode & 0x80) {
        unsigned char released = scancode & 0x7F;
        if (released == 0x1D) {
            g_fallback_ctrl = 0;
        }
        g_fallback_extended = 0;
        return KEYBOARD_ERROR;
    }
/* Legacy Ctrl press check - no modular driver loaded */
    if (scancode == 0x1D) {
        return KEYBOARD_ERROR;
    }

/* Standard key code lookup - only valid when modular driver is loaded */
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

    /* No modular driver loaded - return error */
    return KEYBOARD_ERROR;
}

char get_char(void) {
    cyk_keyboard_driver_t* kbd = modload_get_keyboard_driver();
    if (!kbd || !kbd->scancode_to_ascii) return KEYBOARD_ERROR;
    while (1) {
        char sc = get_scancode();
        char ascii = kbd->scancode_to_ascii((unsigned char)sc);
        if (ascii != 0) {
            return ascii;
        }
        // If we got here, the scancode didn't produce ASCII (e.g., arrow keys,
        // function keys, or release codes). Keep looping to try the next scancode.
        // But we need to make sure we don't infinite-loop on stuck keys.
        // Yield control briefly.
        asm volatile("pause");
    }
    return KEYBOARD_ERROR;
}


