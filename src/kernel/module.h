#ifndef MODULE_H
#define MODULE_H

// CykOS Module Magic: "CYKM" (0x4D4B5943 in little-endian: 'C', 'Y', 'K', 'M')
#define CYKOS_MODULE_MAGIC 0x4D4B5943

// Module Types
#define MODULE_TYPE_UNKNOWN   0x00
#define MODULE_TYPE_KEYBOARD  0x01
#define MODULE_TYPE_DISPLAY   0x02
#define MODULE_TYPE_MOUSE     0x03
#define MODULE_TYPE_SOUND     0x04
#define MODULE_TYPE_USB       0x05
#define MODULE_TYPE_NET       0x06

#ifndef MODULE_LOAD_ADDRESS
#define MODULE_LOAD_ADDRESS   0
#endif

// Generic Level 2 Module Header (Must reside at byte offset 0 of every .drv module)
typedef struct {
    unsigned int magic;         // Must be CYKOS_MODULE_MAGIC
    char name[16];              // Module name (e.g., "cykeyboard")
    unsigned int version;       // Driver version (e.g., 0x00010000 = v1.0)
    unsigned int type;          // Module type (MODULE_TYPE_*)
    unsigned int load_address;  // Runtime address assigned by the module loader
    int (*init)(void);          // Initialization entry point (returns 0 on success)
} cyk_module_header_t;

// Level 2 Keyboard Driver Interface Table
typedef struct {
    cyk_module_header_t header; // Standard base header
    char (*get_scancode)(void); // Blocking: wait and read raw scancode
    char (*scancode_to_ascii)(unsigned char scancode); // Convert scancode to ASCII character
    int (*has_key)(void);       // Non-blocking: 1 if key available in hardware buffer, 0 otherwise
    char (*get_char)(void);     // Blocking: wait for a printable ASCII keypress
    int (*try_get_char)(void);  // Non-blocking: ASCII/ETX byte, or -1 when no character is ready
    void (*reset_state)(void);  // Clear held-key/modifier state after an app exits
} cyk_keyboard_driver_t;

/* USB host module interface. Kernel subsystems call through this table so
 * controller code remains loadable and replaceable as a Level 2 module. */
typedef struct {
    cyk_module_header_t header;
    void (*poll)(void);
    int (*storage_present)(unsigned int device);
    int (*storage_read_sectors)(unsigned int device, unsigned int lba,
                                unsigned char count, void* buffer);
    int (*storage_write_sectors)(unsigned int device, unsigned int lba,
                                 unsigned char count, const void* buffer);
} cyk_usb_driver_t;

// Sound capability flags (bitmask returned by get_caps())
#define SOUND_CAP_NONE     0x00   // No audio capability
#define SOUND_CAP_TONE     0x01   // Can play tone(freq, ms) via PC speaker or similar
#define SOUND_CAP_CHIME    0x02   // Can play chime melody
#define SOUND_CAP_SAMPLE   0x04   // Can play digitized audio samples (HDA, etc.)
#define SOUND_CAP_MUSIC    0x08   // Can play background music / streaming audio
#define SOUND_CAP_VOLUME   0x10   // Volume control supported (0-100)
#define SOUND_CAP_ALL      0xFF

// Kernel IRQ1 keyboard queue consumed by the modular keyboard driver.
int keyboard_irq_has_scancode(void);
int keyboard_irq_read_scancode(void);
void keyboard_irq_reset_state(void);
void keyboard_irq_inject_scancode(unsigned char scancode);
unsigned int idt_get_millis(void);

// Level 2 Sound Driver Interface Table (standardized for game porting)
// Matches the audio abstraction expected by doomgeneric and similar software:
//   tone() / chime()    -> simple beep/melody (PC speaker)
//   play_sample()       -> digitized sound effects (HDA, AC'97, etc.)
//   set_volume() / stop() -> basic audio control
typedef struct {
    cyk_module_header_t header;

    // Core tone API (PC speaker style)
    void (*tone)(unsigned int frequency, unsigned int milliseconds);
    void (*chime)(void);

    // Standardized game audio API
    int  (*set_volume)(unsigned int volume);          // 0-100, returns 0 on success
    int  (*play_sample)(const unsigned char* data, unsigned int len, unsigned int freq_hz);
    int  (*stop)(void);                                // Stop all audio output
    int  (*get_caps)(void);                            // Returns SOUND_CAP_* bitmask

    // Hardware identification string (e.g., "PC Speaker", "Intel HDA Audio")
    char hardware_name[32];
} cyk_sound_driver_t;

// Display pixel formats used by the generic bitmap API.
#define CYK_PIXEL_TEXT16       0
#define CYK_PIXEL_INDEX8       1
#define CYK_PIXEL_VGA_PLANAR4  2

#define CYK_BITMAP_INDEX8      1
#define CYK_BITMAP_RGB332      2
#define CYK_BITMAP_RGB24       3
#define CYK_BITMAP_RGBA32      4
#define CYK_BITMAP_INDEX8_OPAQUE 5

typedef struct {
    unsigned int width;
    unsigned int height;
    unsigned int stride;
    unsigned int format;
    const unsigned char* pixels;
} cyk_bitmap_t;

typedef struct {
    unsigned int width;
    unsigned int height;
    unsigned int pitch;
    unsigned int bits_per_pixel;
    unsigned int pixel_format;
    unsigned char* framebuffer; // NULL when the hardware framebuffer is planar.
} cyk_display_info_t;

// Level 2 Display / Graphics Driver Interface Table
typedef struct {
    cyk_module_header_t header;
    int (*set_mode)(int mode); // 0=text, 1=VGA 640x480x16, 2=VGA 320x200x256; 3 aliases 1
    int (*get_mode)(void);     // Returns active mode (0, 1, or 2)
    void (*put_pixel)(int x, int y, unsigned char color);
    void (*fill_rect)(int x, int y, int w, int h, unsigned char color);
    void (*clear)(unsigned char color);
    void (*draw_bitmap)(int x, int y, int w, int h, const unsigned char* data);
    unsigned char* (*get_framebuffer)(void);
    int (*get_info)(cyk_display_info_t* info);
    int (*draw_bitmap_ex)(int x, int y, const cyk_bitmap_t* bitmap);
    int (*set_palette)(unsigned int first, unsigned int count, const unsigned char* rgb8);
} cyk_display_driver_t;

/* Level 2 Network Interface Controller (NIC) Driver Interface Table */
typedef struct {
    cyk_module_header_t header;
    int (*send_frame)(const unsigned char* data, unsigned int len);
    int (*receive_frame)(unsigned char* out_buf, unsigned int max_len);
    void (*get_mac)(unsigned char out_mac[6]);
    int (*link_up)(void);
    char hardware_name[32];
} cyk_net_driver_t;

#endif // MODULE_H
