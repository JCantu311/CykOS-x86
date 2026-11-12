#ifndef MODLOAD_H
#define MODLOAD_H

#include "module.h"

// Initialize module manager, read /BOOT/MODULES.CFG, and load Level 2 drivers.
int modload_load_all(void);

// Retrieve registered Level 2 keyboard driver (returns NULL if not loaded)
cyk_keyboard_driver_t* modload_get_keyboard_driver(void);

// Retrieve a loaded module by its registered name
cyk_module_header_t* modload_find_module(const char* name);

#endif // MODLOAD_H
