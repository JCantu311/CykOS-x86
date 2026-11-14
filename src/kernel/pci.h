#ifndef PCI_H
#define PCI_H

#include "cykastd.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

// Standard PCI configuration space registers
#define PCI_VENDOR_ID      0x00
#define PCI_DEVICE_ID      0x02
#define PCI_COMMAND        0x04
#define PCI_STATUS         0x06
#define PCI_REVISION_ID    0x08
#define PCI_PROG_IF        0x09
#define PCI_SUBCLASS       0x0A
#define PCI_CLASS          0x0B
#define PCI_BAR0           0x10
#define PCI_BAR1           0x14
#define PCI_BAR2           0x18
#define PCI_BAR3           0x1C
#define PCI_BAR4           0x20
#define PCI_BAR5           0x24
#define PCI_INTERRUPT_LINE 0x3C

// PCI Class Codes
#define PCI_CLASS_MULTIMEDIA 0x04
#define PCI_SUBCLASS_AUDIO   0x01
#define PCI_SUBCLASS_HDA     0x03

typedef struct {
    unsigned char bus;
    unsigned char slot;
    unsigned char func;
    unsigned short vendor_id;
    unsigned short device_id;
    unsigned char class_id;
    unsigned char subclass_id;
    unsigned char prog_if;
    unsigned int bar0;
    unsigned int bar1;
    unsigned char irq;
} pci_device_t;

unsigned int pci_read_config_dword(unsigned char bus, unsigned char slot, unsigned char func, unsigned char offset);
unsigned short pci_read_config_word(unsigned char bus, unsigned char slot, unsigned char func, unsigned char offset);
unsigned char pci_read_config_byte(unsigned char bus, unsigned char slot, unsigned char func, unsigned char offset);
void pci_write_config_dword(unsigned char bus, unsigned char slot, unsigned char func, unsigned char offset, unsigned int val);
void pci_write_config_word(unsigned char bus, unsigned char slot, unsigned char func, unsigned char offset, unsigned short val);

int pci_find_audio_device(pci_device_t* out_dev);

#endif // PCI_H
