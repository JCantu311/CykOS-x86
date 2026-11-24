#ifndef CYKOS_USB_H
#define CYKOS_USB_H

/* Kernel-facing shims delegated to the loaded USB Level 2 module. */
void usb_poll(void);

#define USB_STORAGE_DRIVE_BASE 4u
#define USB_STORAGE_MAX_DEVICES 8u
int usb_storage_present(unsigned int device);
int usb_storage_read_sectors(unsigned int device, unsigned int lba,
                             unsigned char count, void* buffer);
int usb_storage_write_sectors(unsigned int device, unsigned int lba,
                              unsigned char count, const void* buffer);

#endif
