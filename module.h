#ifndef MODULE_H
#define MODULE_H

// CykOS Module Magic: "CYKM" (0x4D4B5943 in little-endian: 'C', 'Y', 'K', 'M')
#define CYKOS_MODULE_MAGIC 0x4D4B5943

// Module Types
#define MODULE_TYPE_UNKNOWN   0x00
#define MODULE_TYPE_KEYBOARD  0x01
#define MODULE_TYPE_DISPLAY   0x02
#define MODULE_TYPE_MOUSE     0x03

#ifndef MODULE_LOAD_ADDRESS
#define MODULE_LOAD_ADDRESS   0x40000
#endif

// Generic Level 2 Module Header (Must reside at byte offset 0 of every .drv module)
typedef struct {
    unsigned int magic;         // Must be CYKOS_MODULE_MAGIC
    char name[16];              // Module name (e.g., "cykeyboard")
    unsigned int version;       // Driver version (e.g., 0x00010000 = v1.0)
    unsigned int type;          // Module type (MODULE_TYPE_*)
    unsigned int load_address;  // Fixed address the module was linked for
    int (*init)(void);          // Initialization entry point (returns 0 on success)
} cyk_module_header_t;

// Level 2 Keyboard Driver Interface Table
typedef struct {
    cyk_module_header_t header; // Standard base header
    char (*get_scancode)(void); // Blocking: wait and read raw scancode
    char (*scancode_to_ascii)(unsigned char scancode); // Convert scancode to ASCII character
    int (*has_key)(void);       // Non-blocking: 1 if key available in hardware buffer, 0 otherwise
    char (*get_char)(void);     // Blocking: wait for a printable ASCII keypress
} cyk_keyboard_driver_t;

#endif // MODULE_H
