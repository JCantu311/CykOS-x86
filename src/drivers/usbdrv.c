#include "module.h"

int usb_stack_init(void);
void usb_stack_poll(void);
int usb_stack_storage_present(unsigned int device);
int usb_stack_storage_read_sectors(unsigned int device, unsigned int lba,
                                  unsigned char count, void* buffer);
int usb_stack_storage_write_sectors(unsigned int device, unsigned int lba,
                                   unsigned char count, const void* buffer);

/* The controller implementations are built into this Level 2 driver image. */
#include "../kernel/xhci.c"
#include "../kernel/usb.c"

static int usb_module_init(void) {
    return usb_stack_init() < 0 ? -1 : 0;
}

__attribute__((section(".header"), used))
const cyk_usb_driver_t cyk_usb_driver = {
    .header = {
        .magic = CYKOS_MODULE_MAGIC,
        .name = "cykusb",
        .version = 0x00010000,
        .type = MODULE_TYPE_USB,
        .load_address = MODULE_LOAD_ADDRESS,
        .init = usb_module_init,
    },
    .poll = usb_stack_poll,
    .storage_present = usb_stack_storage_present,
    .storage_read_sectors = usb_stack_storage_read_sectors,
    .storage_write_sectors = usb_stack_storage_write_sectors,
};
