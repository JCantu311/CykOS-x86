#include "modload.h"
#include "fat16.h"
#include "vfs.h"
#include "cykastd.h"

#define MAX_MODULES         4
#define MODULE_SLOT_SIZE    0x4000    // 16KB per module
#define MODULE_BASE_ADDR    0x40000   // Level 2 memory base

static cyk_module_header_t* g_loaded_modules[MAX_MODULES];
static unsigned char g_used_slots[MAX_MODULES];
static int g_loaded_count = 0;
static cyk_keyboard_driver_t* g_keyboard_driver = (void*)0;

// Internal helper to trim whitespace and extract next word
static const char* parse_next_token(const char* p, char* out_token, int max_len) {
    // Skip leading whitespace and newlines
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }

    if (*p == '\0') {
        return (void*)0;
    }

    // Skip comment line
    if (*p == '#') {
        while (*p != '\0' && *p != '\n') {
            p++;
        }
        return parse_next_token(p, out_token, max_len);
    }

    int i = 0;
    while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && i < max_len - 1) {
        out_token[i++] = *p++;
    }
    out_token[i] = '\0';

    return p;
}

int modload_load_all(void) {
    g_loaded_count = 0;
    memset(g_loaded_modules, 0, sizeof(g_loaded_modules));
    memset(g_used_slots, 0, sizeof(g_used_slots));
    g_keyboard_driver = (void*)0;

    print_string("[MODLOAD] Checking for /BOOT/MODULES.CFG...\n");

    vfs_file_t cfg_entry;
    if (vfs_find_file("/BOOT/MODULES.CFG", &cfg_entry) != 0 &&
        vfs_find_file("/CYKOS/MODULES.CFG", &cfg_entry) != 0) {
        print_color_string("[MODLOAD] Note: MODULES.CFG not found on disk.\n", VGA_RED);
        return 0;
    }

    // Read MODULES.CFG into temporary scratch buffer
    char cfg_buf[512];
    memset(cfg_buf, 0, sizeof(cfg_buf));
    int bytes_read = vfs_read_at(&cfg_entry, 0, cfg_buf, sizeof(cfg_buf) - 1);
    if (bytes_read <= 0) {
        print_color_string("[MODLOAD] Warning: MODULES.CFG is empty.\n", VGA_RED);
        return 0;
    }

    const char* cursor = cfg_buf;
    char mod_filename[64];

    while ((cursor = parse_next_token(cursor, mod_filename, sizeof(mod_filename))) != (void*)0) {
        if (g_loaded_count >= MAX_MODULES) {
            print_color_string("[MODLOAD] Maximum module capacity reached.\n", VGA_RED);
            break;
        }

        print_string("[MODLOAD] Loading module: ");
        print_string(mod_filename);
        print_string("...\n");

        vfs_file_t mod_entry;
        if (vfs_find_file(mod_filename, &mod_entry) != 0) {
            print_color_string(" -> Error: File not found on filesystem!\n", VGA_RED);
            continue;
        }

        if (mod_entry.size < sizeof(cyk_module_header_t) || mod_entry.size > MODULE_SLOT_SIZE) {
            print_color_string(" -> Error: Module size is outside the supported slot size.\n", VGA_RED);
            continue;
        }

        cyk_module_header_t preview;
        memset(&preview, 0, sizeof(preview));
        if (vfs_read_at(&mod_entry, 0, &preview, sizeof(preview)) != (int)sizeof(preview) ||
            preview.magic != CYKOS_MODULE_MAGIC) {
            print_color_string(" -> Error: Invalid module header.\n", VGA_RED);
            continue;
        }

        if (preview.load_address < MODULE_BASE_ADDR ||
            (preview.load_address - MODULE_BASE_ADDR) % MODULE_SLOT_SIZE != 0) {
            print_color_string(" -> Error: Module load address is not a valid slot.\n", VGA_RED);
            continue;
        }
        unsigned int slot_index = (preview.load_address - MODULE_BASE_ADDR) / MODULE_SLOT_SIZE;
        if (slot_index >= MAX_MODULES) {
            print_color_string(" -> Error: Module load address is outside the module region.\n", VGA_RED);
            continue;
        }
        if (g_used_slots[slot_index]) {
            print_color_string(" -> Error: Module slot is already occupied.\n", VGA_RED);
            continue;
        }

        void* target_dest = (void*)preview.load_address;
        memset(target_dest, 0, MODULE_SLOT_SIZE);

        int mod_bytes = vfs_read_at(&mod_entry, 0, target_dest, MODULE_SLOT_SIZE);
        if (mod_bytes <= 0) {
            print_color_string(" -> Error: Failed to read module binary!\n", VGA_RED);
            continue;
        }

        // Validate module header magic
        cyk_module_header_t* header = (cyk_module_header_t*)target_dest;
        if (header->magic != CYKOS_MODULE_MAGIC || header->load_address != preview.load_address) {
            print_color_string(" -> Error: Invalid module magic header!\n", VGA_RED);
            continue;
        }

        g_used_slots[slot_index] = 1;

        // Execute module initialization
        if (header->init != (void*)0) {
            int init_res = header->init();
            if (init_res != 0) {
                print_color_string(" -> Error: Module init() returned failure!\n", VGA_RED);
                continue;
            }
        }

        // Register driver by type
        if (header->type == MODULE_TYPE_KEYBOARD) {
            g_keyboard_driver = (cyk_keyboard_driver_t*)header;
        }

        g_loaded_modules[g_loaded_count] = header;
        g_loaded_count++;

        print_color_string(" -> Registered: ", VGA_GREEN);
        print_string(header->name);
        print_color_string(" [OK]\n", VGA_GREEN);
    }

    return g_loaded_count;
}

cyk_keyboard_driver_t* modload_get_keyboard_driver(void) {
    return g_keyboard_driver;
}

cyk_module_header_t* modload_find_module(const char* name) {
    if (!name) return (void*)0;
    for (int i = 0; i < g_loaded_count; i++) {
        if (strncmp(g_loaded_modules[i]->name, name, 16) == 0) {
            return g_loaded_modules[i];
        }
    }
    return (void*)0;
}
