#ifndef MODLOAD_H
#define MODLOAD_H

#include "module.h"

// Initialize module manager, read /BOOT/MODULES.CFG, and load Level 2 drivers.
int modload_load_all(void);

// Retrieve registered Level 2 keyboard driver (returns NULL if not loaded)
cyk_keyboard_driver_t* modload_get_keyboard_driver(void);

// Retrieve the best available Level 2 sound driver (auto-selected by capability)
// Returns NULL if no sound driver is loaded.
cyk_sound_driver_t* modload_get_sound_driver(void);

// Retrieve all loaded sound drivers (up to max_drivers), returns count written.
// Useful for manual driver selection by applications.
int modload_get_all_sound_drivers(cyk_sound_driver_t** out_drivers, int max_drivers);

// Select a specific sound driver by index (0 = first loaded, etc.)
// Returns 0 on success, -1 if index is out of range.
int modload_select_sound_driver(int index);

// Retrieve registered Level 2 display driver (returns NULL if not loaded)
cyk_display_driver_t* modload_get_display_driver(void);

// Retrieve a loaded module by its registered name
cyk_module_header_t* modload_find_module(const char* name);

#endif // MODLOAD_H
