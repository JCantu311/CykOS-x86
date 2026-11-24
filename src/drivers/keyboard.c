#include "module.h"
#include "cykastd.h"

#define KBD_DATA_PORT    0x60
#define KBD_STATUS_PORT  0x64
#define KEY_SHIFT_EVENT  0x8A

static inline unsigned char kbd_inb(unsigned short port) {
    unsigned char result;
    asm volatile("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static inline void kbd_outb(unsigned short port, unsigned char data) {
    asm volatile("outb %0, %1" :: "a"(data), "Nd"(port));
}

static int kbd_wait_input_empty(void) {
    for (unsigned int i = 0; i < 100000; ++i)
        if ((kbd_inb(KBD_STATUS_PORT) & 0x02u) == 0) return 0;
    return -1;
}

static int kbd_wait_output_full(unsigned char* value) {
    for (unsigned int i = 0; i < 100000; ++i) {
        if (kbd_inb(KBD_STATUS_PORT) & 0x01u) {
            *value = kbd_inb(KBD_DATA_PORT);
            return 0;
        }
    }
    return -1;
}

static int kbd_write_command(unsigned char value) {
    if (kbd_wait_input_empty() != 0) return -1;
    kbd_outb(KBD_STATUS_PORT, value);
    return 0;
}

static int kbd_write_data(unsigned char value) {
    if (kbd_wait_input_empty() != 0) return -1;
    kbd_outb(KBD_DATA_PORT, value);
    return 0;
}

static int kbd_send_with_ack(unsigned char value) {
    for (unsigned int attempt = 0; attempt < 3; ++attempt) {
        unsigned char response;
        if (kbd_write_data(value) != 0 || kbd_wait_output_full(&response) != 0)
            return -1;
        if (response == 0xFA) return 0;
        if (response != 0xFE) return -1;
    }
    return -1;
}

/* Configure the PS/2 keyboard to emit set 1 scan codes through the i8042.
 * IRQ1 is disabled during the controller/device command exchange so ACKs are
 * consumed here instead of mistaken for key events by the IRQ handler. */
static void kbd_configure_set1_stream(void) {
    unsigned int saved_flags;
    asm volatile("pushfl; popl %0; cli" : "=r"(saved_flags) :: "memory");
    unsigned char command_byte = 0;
    unsigned char response;
    int have_command_byte = 0;

    if (kbd_write_command(0x20) == 0 && kbd_wait_output_full(&command_byte) == 0) {
        have_command_byte = 1;
        command_byte = (unsigned char)((command_byte | 0x40u) & ~0x11u);
        if (kbd_write_command(0x60) == 0 && kbd_write_data(command_byte) == 0) {
            if (kbd_send_with_ack(0xF0) == 0) (void)kbd_send_with_ack(0x02);
            (void)kbd_send_with_ack(0xF4);
        }
    }
    if (have_command_byte && kbd_write_command(0x20) == 0 &&
        kbd_wait_output_full(&response) == 0) {
        response = (unsigned char)((response | 0x41u) & ~0x10u);
        if (kbd_write_command(0x60) == 0) (void)kbd_write_data(response);
    }
    if (saved_flags & 0x200u) asm volatile("sti" ::: "memory");
}

// Internal modifier states
static int g_shift_state = 0;
static int g_ctrl_state = 0;
static int g_caps_lock = 0;
static int g_extended = 0;
static unsigned char g_pressed_keys[256];

static void keyboard_reset_state(void) {
    g_shift_state = 0;
    g_ctrl_state = 0;
    g_extended = 0;
    memset(g_pressed_keys, 0, sizeof(g_pressed_keys));
}

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
    return keyboard_irq_has_scancode() || (kbd_inb(KBD_STATUS_PORT) & 0x01u);
}

/* Consume the IRQ queue first, then poll the i8042 as a fallback. Polling is
 * done with IRQ1 briefly disabled so the interrupt handler cannot race the
 * read from port 0x60 and lose a scancode. */
static int keyboard_try_read_scancode(void) {
    unsigned int saved_flags;
    asm volatile("pushfl; popl %0; cli" : "=r"(saved_flags) :: "memory");
    int scancode = keyboard_irq_read_scancode();
    if (scancode < 0 && (kbd_inb(KBD_STATUS_PORT) & 0x01u))
        scancode = kbd_inb(KBD_DATA_PORT);
    if (saved_flags & 0x200u) asm volatile("sti" ::: "memory");
    return scancode;
}

// Blocking read of next raw scancode, using IRQ buffering or controller polling.
static char keyboard_get_scancode(void) {
    int scancode;
    while ((scancode = keyboard_try_read_scancode()) < 0) {
        asm volatile("pause");
    }
    return (char)scancode;
}

// Convert Set 1 scancodes into stable keydown/keyup events. The encoded
// keyup range preserves real key holds for applications such as Doom.
static int keyboard_scancode_to_event(unsigned char scancode) {
    // Extended scancode escape prefix (e.g., arrow keys)
    if (scancode == 0xE0) {
        g_extended = 1;
        return 0;
    }
    if (scancode == 0xE1) {
        g_extended = 0;
        return 0;
    }

    unsigned int key_index = (g_extended ? 128u : 0u) + (scancode & 0x7Fu);
    // Key releases (break codes)
    if (scancode & 0x80) {
        unsigned char released = scancode & 0x7F;
        if (released == 0x2A || released == 0x36) { // Left or Right Shift release
            g_shift_state = 0;
        }
        if (released == 0x1D) { // Left or Right Ctrl release
            g_ctrl_state = 0;
        }
        unsigned int alternate_index = key_index ^ 128u;
        unsigned char key = g_pressed_keys[key_index];
        if (!key) key = g_pressed_keys[alternate_index];
        g_pressed_keys[key_index] = 0;
        g_pressed_keys[alternate_index] = 0;
        g_extended = 0;
        return key ? 0x100 + key : 0;
    }

    // Key presses (make codes)
    g_extended = 0;
    if (scancode == 0x2A || scancode == 0x36) { // Left or Right Shift press
        g_shift_state = 1;
        g_pressed_keys[key_index] = KEY_SHIFT_EVENT;
        return KEY_SHIFT_EVENT;
    }

    if (scancode == 0x1D) { // Left or Right Ctrl press
        g_ctrl_state = 1;
        g_pressed_keys[key_index] = 0xA3; // Doom's default KEY_FIRE
        return 0xA3;
    }

    if (scancode == 0x3A) { // Caps Lock toggle
        g_caps_lock = !g_caps_lock;
        return 0;
    }

    // Navigation and editing keys (both dedicated extended cluster and keypad)
    int key;
    switch (scancode) {
        case 0x48: key = KEY_UP; break;
        case 0x50: key = KEY_DOWN; break;
        case 0x4B: key = KEY_LEFT; break;
        case 0x4D: key = KEY_RIGHT; break;
        case 0x47: key = KEY_HOME; break;
        case 0x4F: key = KEY_END; break;
        case 0x49: key = KEY_PAGEUP; break;
        case 0x51: key = KEY_PAGEDOWN; break;
        case 0x52: key = KEY_INSERT; break;
        case 0x53: key = KEY_DELETE; break;
        default: break;
    }

    if (scancode == 0x48 || scancode == 0x50 || scancode == 0x4B ||
        scancode == 0x4D || scancode == 0x47 || scancode == 0x4F ||
        scancode == 0x49 || scancode == 0x51 || scancode == 0x52 ||
        scancode == 0x53) {
        g_pressed_keys[key_index] = (unsigned char)key;
        return key;
    }

    if (scancode >= 128) {
        return 0;
    }

    if (g_ctrl_state) {
        if (scancode == 0x2E) { // 'c' / 'C'
            key = 0x03;          // ETX (Ctrl+C)
        } else {
            char normal = scancode_normal[scancode];
            key = (normal >= 'a' && normal <= 'z') ? normal - 'a' + 1 : 0;
        }
        if (key) g_pressed_keys[key_index] = (unsigned char)key;
        return key;
    }

    char normal_c = scancode_normal[scancode];
    char shifted_c = scancode_shifted[scancode];

    // For alphabetical characters, handle Shift XOR Caps Lock
    if (normal_c >= 'a' && normal_c <= 'z') {
        int uppercase = g_shift_state ^ g_caps_lock;
        key = uppercase ? shifted_c : normal_c;
    } else {
        // For symbols and numbers, Shift determines shifted character
        key = g_shift_state ? shifted_c : normal_c;
    }
    if (key) g_pressed_keys[key_index] = (unsigned char)key;
    return key;
}

// Character APIs intentionally suppress keyup events.
static char keyboard_scancode_to_ascii(unsigned char scancode) {
    int event = keyboard_scancode_to_event(scancode);
    // Preserve control keys expected by the shell and POSIX line input.
    if (event == '\b' || event == '\t' || event == '\n' || event == '\r' ||
        event == 0x03 || event == 0x0C || event == 0x1B ||
        (event >= 0x80 && event <= 0x89)) return (char)event;
    if (event >= 0x20 && event < 0x7F) return (char)event;
    return 0;
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

// Non-blocking character read for games and other frame-based applications.
static int keyboard_try_get_char(void) {
    int scancode;
    while ((scancode = keyboard_try_read_scancode()) >= 0) {
        char character = keyboard_scancode_to_ascii((unsigned char)scancode);
        if (character != 0) return (unsigned char)character;
    }
    return -1;
}

// Hardware Driver Initialization
static int keyboard_init(void) {
    /* Clear stale controller output before negotiating the PS/2 scan set. */
    unsigned int saved_flags;
    asm volatile("pushfl; popl %0; cli" : "=r"(saved_flags) :: "memory");
    /* Some emulators/controllers can leave OBF asserted after a read. Never
     * let stale-output cleanup trap the boot process in an unbounded loop. */
    for (unsigned int drain = 0; drain < 32u; ++drain) {
        if (!(kbd_inb(KBD_STATUS_PORT) & 0x01u)) break;
        (void)kbd_inb(KBD_DATA_PORT);
    }
    kbd_configure_set1_stream();
    g_shift_state = 0;
    g_ctrl_state = 0;
    g_caps_lock = 0;
    g_extended = 0;
    memset(g_pressed_keys, 0, sizeof(g_pressed_keys));
    if (saved_flags & 0x200u) asm volatile("sti" ::: "memory");

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
    .try_get_char = keyboard_try_get_char,
    .reset_state = keyboard_reset_state,
};
