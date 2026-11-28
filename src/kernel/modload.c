#include "modload.h"
#include "fat16.h"
#include "vfs.h"
#include "cykastd.h"
#include "usb.h"
#include "usercmd.h"
#include "heap.h"

#define MAX_MODULES 8
#define MODULE_FILE_MAX_SIZE 0x00080000u
#define MODULE_IMAGE_CAPACITY 0x00040000u
#define MODULE_PAGE_SIZE 4096u

static cyk_module_header_t* g_loaded_modules[MAX_MODULES];
static void* g_module_allocations[MAX_MODULES];
static int g_loaded_count = 0;
static cyk_keyboard_driver_t* g_keyboard_driver = (void*)0;
static cyk_sound_driver_t* g_sound_drivers[MAX_MODULES];
static int g_sound_driver_count = 0;
static int g_selected_sound_index = 0;
static cyk_display_driver_t* g_display_driver = (void*)0;
static cyk_usb_driver_t* g_usb_driver = (void*)0;
static cyk_net_driver_t* g_net_driver = (void*)0;
static const char* parse_next_token(const char* p, char* out_token, int max_len) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p == '\0') return (void*)0;
    if (*p == '#') { while (*p && *p != '\n') p++; return parse_next_token(p, out_token, max_len); }
    int i = 0;
    while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && i < max_len - 1) out_token[i++] = *p++;
    out_token[i] = '\0';
    return p;
}
int modload_load_all(void) {
    g_loaded_count = 0;
    memset(g_loaded_modules, 0, sizeof(g_loaded_modules));
    memset(g_module_allocations, 0, sizeof(g_module_allocations));
    g_keyboard_driver = (void*)0;
    g_sound_driver_count = 0;
    g_selected_sound_index = 0;
    memset(g_sound_drivers, 0, sizeof(g_sound_drivers));
    g_display_driver = (void*)0;
    g_usb_driver = (void*)0;
    g_net_driver = (void*)0;

    print_string("[MODLOAD] Checking for /BOOT/MODULES.CFG...\n");

    vfs_file_t cfg_entry;
    if (vfs_find_file("/BOOT/MODULES.CFG", &cfg_entry) != 0) {
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
        print_string(" -> Module file found; staging image...\n");

        if (mod_entry.size < 52u || mod_entry.size > MODULE_FILE_MAX_SIZE) {
            print_color_string(" -> Error: Module file size is outside the supported range.\n", VGA_RED);
            continue;
        }

        unsigned char* file_image = (unsigned char*)malloc(mod_entry.size);
        void* allocation = malloc(MODULE_IMAGE_CAPACITY + MODULE_PAGE_SIZE - 1u);
        if (!file_image || !allocation) {
            if (file_image) free(file_image);
            if (allocation) free(allocation);
            print_color_string(" -> Error: Not enough memory for module.\n", VGA_RED);
            continue;
        }
        int mod_bytes = vfs_read_at(&mod_entry, 0, file_image, mod_entry.size);
        if (mod_bytes != (int)mod_entry.size) {
            free(file_image);
            free(allocation);
            print_color_string(" -> Error: Failed to read complete module file.\n", VGA_RED);
            continue;
        }
        print_string(" -> File staged; applying ELF relocations...\n");

        unsigned int aligned = ((unsigned int)allocation + MODULE_PAGE_SIZE - 1u) & ~(MODULE_PAGE_SIZE - 1u);
        void* image = (void*)aligned;
        memset(image, 0, MODULE_IMAGE_CAPACITY);
        int load_result = module_elf32_load(file_image, mod_entry.size, image,
                                            MODULE_IMAGE_CAPACITY);
        free(file_image);
        if (load_result != 0) {
            free(allocation);
            print_color_string(" -> Error: Invalid or unsupported ELF module.\n", VGA_RED);
            continue;
        }
        print_string(" -> ELF relocated; validating module header...\n");

        cyk_module_header_t* header = (cyk_module_header_t*)0;
        for (unsigned int offset = 0; offset + sizeof(cyk_module_header_t) <= MODULE_IMAGE_CAPACITY;
             offset += 4u) {
            cyk_module_header_t* candidate = (cyk_module_header_t*)(aligned + offset);
            if (candidate->magic == CYKOS_MODULE_MAGIC) {
                header = candidate;
                break;
            }
        }
        unsigned int image_end = aligned + MODULE_IMAGE_CAPACITY;
        int name_terminated = 0;
        if (header) {
            for (unsigned int i = 0; i < sizeof(header->name); i++)
                if (!header->name[i]) { name_terminated = 1; break; }
        }
        if (!header || !header->name[0] ||
            !name_terminated ||
            (header->type != MODULE_TYPE_UNKNOWN && header->type != MODULE_TYPE_KEYBOARD &&
             header->type != MODULE_TYPE_DISPLAY &&
             header->type != MODULE_TYPE_SOUND && header->type != MODULE_TYPE_USB &&
             header->type != MODULE_TYPE_NET) ||
            !header->init || (unsigned int)header->init < aligned ||
            (unsigned int)header->init >= image_end) {
            free(allocation);
            print_color_string(" -> Error: Relocated image has no valid module header.\n", VGA_RED);
            continue;
        }
        header->load_address = (unsigned int)header;

        print_string(" -> Initializing module...\n");
        if (header->init != (void*)0) {
            int init_res = header->init();
            if (init_res != 0) {
                /* Initialization can register IRQ callbacks or claim hardware;
                 * leave the allocation resident if init reports failure. */
                print_color_string(" -> Error: Module init() returned failure!\n", VGA_RED);
                continue;
            }
        }

        if (header->type == MODULE_TYPE_KEYBOARD) {
            g_keyboard_driver = (cyk_keyboard_driver_t*)header;
        } else if (header->type == MODULE_TYPE_SOUND) {
            if (g_sound_driver_count < MAX_MODULES) {
                g_sound_drivers[g_sound_driver_count++] = (cyk_sound_driver_t*)header;
            }
        } else if (header->type == MODULE_TYPE_DISPLAY) {
            g_display_driver = (cyk_display_driver_t*)header;
        } else if (header->type == MODULE_TYPE_USB) {
            g_usb_driver = (cyk_usb_driver_t*)header;
        } else if (header->type == MODULE_TYPE_NET) {
            g_net_driver = (cyk_net_driver_t*)header;
        }

        g_module_allocations[g_loaded_count] = allocation;
        g_loaded_modules[g_loaded_count] = header;
        g_loaded_count++;

        print_color_string(" -> Registered: ", VGA_GREEN);
        print_string(header->name);
        print_color_string(" [OK]\n", VGA_GREEN);
    }

    if (g_sound_driver_count > 0) {
        int best_index = 0;
        int best_caps = -1;
        for (int i = 0; i < g_sound_driver_count; i++) {
            cyk_sound_driver_t* drv = g_sound_drivers[i];
            int caps = 0;
            if (drv && drv->get_caps) {
                caps = drv->get_caps();
            }
            if (caps > best_caps) {
                best_caps = caps;
                best_index = i;
            }
        }
        g_selected_sound_index = best_index;

        cyk_sound_driver_t* best = g_sound_drivers[best_index];
        print_color_string("[MODLOAD] Auto-selected sound driver: ", VGA_GREEN);
        print_string(best ? best->header.name : "(none)");
        if (best && best->hardware_name[0]) {
            print_string(" (");
            print_string(best->hardware_name);
            print_string(")");
        }
        print_char('\n');
    }

    /* Keep the module API shim in the kernel symbol map for other modules. */
    usb_poll();

    return g_loaded_count;
}

void usb_poll(void) {
    if (g_usb_driver && g_usb_driver->poll) g_usb_driver->poll();
}

int usb_storage_present(unsigned int device) {
    return g_usb_driver && g_usb_driver->storage_present
        ? g_usb_driver->storage_present(device) : 0;
}

int usb_storage_read_sectors(unsigned int device, unsigned int lba,
                             unsigned char count, void* buffer) {
    return g_usb_driver && g_usb_driver->storage_read_sectors
        ? g_usb_driver->storage_read_sectors(device, lba, count, buffer) : -1;
}

int usb_storage_write_sectors(unsigned int device, unsigned int lba,
                              unsigned char count, const void* buffer) {
    return g_usb_driver && g_usb_driver->storage_write_sectors
        ? g_usb_driver->storage_write_sectors(device, lba, count, buffer) : -1;
}
cyk_keyboard_driver_t* modload_get_keyboard_driver(void) {
    return g_keyboard_driver;
}

cyk_sound_driver_t* modload_get_sound_driver(void) {
    if (g_sound_driver_count == 0) return (void*)0;
    if (g_selected_sound_index < 0 || g_selected_sound_index >= g_sound_driver_count) {
        g_selected_sound_index = 0;
    }
    return g_sound_drivers[g_selected_sound_index];
}

int modload_get_all_sound_drivers(cyk_sound_driver_t** out_drivers, int max_drivers) {
    if (!out_drivers || max_drivers <= 0) return 0;
    int count = g_sound_driver_count < max_drivers ? g_sound_driver_count : max_drivers;
    for (int i = 0; i < count; i++) {
        out_drivers[i] = g_sound_drivers[i];
    }
    return count;
}

int modload_select_sound_driver(int index) {
    if (index < 0 || index >= g_sound_driver_count) return -1;
    g_selected_sound_index = index;
    return 0;
}

cyk_display_driver_t* modload_get_display_driver(void) {
    return g_display_driver;
}

cyk_net_driver_t* modload_get_net_driver(void) {
    return g_net_driver;
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

