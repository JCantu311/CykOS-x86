#include "module.h"
#include "fat16.h"
#include "cykastd.h"

static int disk_mount_init(void) {
    int mounted = fat16_mount_extra_drives();
    if (mounted < 0) {
        print_color_string("[DISK] Volume discovery failed.\n", VGA_RED);
        return -1;
    }
    print_string("[DISK] Additional FAT16 volumes mounted: ");
    print_char((char)('0' + (mounted % 10)));
    print_string("\n");
    return 0;
}

__attribute__((section(".header"), used))
const cyk_module_header_t cyk_disk_driver = {
    .magic = CYKOS_MODULE_MAGIC,
    .name = "cydisk",
    .version = 0x00010000,
    .type = MODULE_TYPE_UNKNOWN,
    .load_address = MODULE_LOAD_ADDRESS,
    .init = disk_mount_init,
};
