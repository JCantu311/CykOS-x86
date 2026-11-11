#include "module.h"

// Hardware I/O Port Definitions
#define KBD_DATA_PORT    0x60
#define KBD_STATUS_PORT  0x64

// Internal port access (keeps driver 100% self-contained)
static inline unsigned char kbd_inb(unsigned short port) {
    unsigned char result;
    asm volatile("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static inline void kbd_outb(unsigned short port, unsigned char data) {
    asm volatile("outb %0, %1" :: "a"(data), "Nd"(port));
}

// Internal modifier states
static int g_shift_state = 0;
static int g_caps_lock = 0;
static int g_extended = 0;

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

// Standard US QWERTY Scancode Set 1 Lookup Tables
static const char scancode_normal[128] = {
    0,   27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,   'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,   '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0,   ' ', 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
};

static const char scancode_shifted[128] = {
    0,   27,  '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,   'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,   '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0,   ' ', 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
};

// Check if keyboard buffer has data ready to read (non-blocking)
static int keyboard_has_key(void) {
    return (kbd_inb(KBD_STATUS_PORT) & 0x01) != 0;
}

// Blocking read of next raw scancode
static char keyboard_get_scancode(void) {
    while (!keyboard_has_key()) {
        asm volatile("pause");
    }
    return (char)kbd_inb(KBD_DATA_PORT);
}

// Convert Set 1 scancode to ASCII character, tracking Shift and Caps Lock
static char keyboard_scancode_to_ascii(unsigned char scancode) {
    // Extended scancode escape prefix (e.g., arrow keys)
    if (scancode == 0xE0) {
        g_extended = 1;
        return 0;
    }
    if (scancode == 0xE1) {
        return 0;
    }

    // Key releases (break codes)
    if (scancode & 0x80) {
        unsigned char released = scancode & 0x7F;
        if (released == 0x2A || released == 0x36) { // Left or Right Shift release
            g_shift_state = 0;
        }
        g_extended = 0;
        return 0;
    }

    // Key presses (make codes)
    g_extended = 0;

    if (scancode == 0x2A || scancode == 0x36) { // Left or Right Shift press
        g_shift_state = 1;
        return 0;
    }

    if (scancode == 0x3A) { // Caps Lock toggle
        g_caps_lock = !g_caps_lock;
        return 0;
    }

    // Navigation and editing keys (both dedicated extended cluster and keypad)
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

    if (scancode >= 128) {
        return 0;
    }

    char normal_c = scancode_normal[scancode];
    char shifted_c = scancode_shifted[scancode];

    // For alphabetical characters, handle Shift XOR Caps Lock
    if (normal_c >= 'a' && normal_c <= 'z') {
        int uppercase = g_shift_state ^ g_caps_lock;
        return uppercase ? shifted_c : normal_c;
    }

    // For symbols and numbers, Shift determines shifted character
    return g_shift_state ? shifted_c : normal_c;
}

// Blocking get character (convenience function for Shell and applications)
static char keyboard_get_char(void) {
    while (1) {
        char sc = keyboard_get_scancode();
        char ascii = keyboard_scancode_to_ascii((unsigned char)sc);
        if (ascii != 0) {
            return ascii;
        }
    }
}

// Hardware Driver Initialization
static int keyboard_init(void) {
    // 1. Drain the controller output buffer
    while (kbd_inb(KBD_STATUS_PORT) & 0x01) {
        kbd_inb(KBD_DATA_PORT);
    }

    // 2. Reset internal states
    g_shift_state = 0;
    g_caps_lock = 0;
    g_extended = 0;

    return 0; // Success
}

// Module Header: Emitted directly at the start of the binary (.header section)
__attribute__((section(".header"), used))
const cyk_keyboard_driver_t cyk_keyboard_driver = {
    .header = {
        .magic = CYKOS_MODULE_MAGIC,
        .name = "cykeyboard",
        .version = 0x00010000,
        .type = MODULE_TYPE_KEYBOARD,
        .load_address = MODULE_LOAD_ADDRESS,
        .init = keyboard_init,
    },
    .get_scancode = keyboard_get_scancode,
    .scancode_to_ascii = keyboard_scancode_to_ascii,
    .has_key = keyboard_has_key,
    .get_char = keyboard_get_char,
};
