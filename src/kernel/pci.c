#include "pci.h"

unsigned int pci_read_config_dword(unsigned char bus, unsigned char slot, unsigned char func, unsigned char offset) {
    unsigned int address = (unsigned int)((bus << 16) | (slot << 11) |
              (func << 8) | (offset & 0xFC) | ((unsigned int)0x80000000));
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

unsigned short pci_read_config_word(unsigned char bus, unsigned char slot, unsigned char func, unsigned char offset) {
    unsigned int dword = pci_read_config_dword(bus, slot, func, offset);
    return (unsigned short)((dword >> ((offset & 2) * 8)) & 0xFFFF);
}

unsigned char pci_read_config_byte(unsigned char bus, unsigned char slot, unsigned char func, unsigned char offset) {
    unsigned int dword = pci_read_config_dword(bus, slot, func, offset);
    return (unsigned char)((dword >> ((offset & 3) * 8)) & 0xFF);
}

void pci_write_config_dword(unsigned char bus, unsigned char slot, unsigned char func, unsigned char offset, unsigned int val) {
    unsigned int address = (unsigned int)((bus << 16) | (slot << 11) |
              (func << 8) | (offset & 0xFC) | ((unsigned int)0x80000000));
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, val);
}

void pci_write_config_word(unsigned char bus, unsigned char slot, unsigned char func, unsigned char offset, unsigned short val) {
    unsigned int cur = pci_read_config_dword(bus, slot, func, offset);
    unsigned int shift = (offset & 2) * 8;
    cur &= ~(0xFFFF << shift);
    cur |= ((unsigned int)val << shift);
    pci_write_config_dword(bus, slot, func, offset, cur);
}

int pci_find_class_device(unsigned char wanted_class, unsigned char wanted_subclass,
                          unsigned char wanted_prog_if, pci_device_t* out_dev) {
    for (unsigned int bus = 0; bus < 256; ++bus) {
        for (unsigned char slot = 0; slot < 32; ++slot) {
            unsigned short vendor0 = pci_read_config_word((unsigned char)bus,
                                                           slot, 0, PCI_VENDOR_ID);
            if (vendor0 == 0xFFFF || vendor0 == 0) continue;
            unsigned char functions =
                (pci_read_config_byte((unsigned char)bus, slot, 0, 0x0E) & 0x80u)
                    ? 8u : 1u;
            for (unsigned char func = 0; func < functions; ++func) {
                unsigned short vendor = func == 0 ? vendor0 :
                    pci_read_config_word((unsigned char)bus, slot, func, PCI_VENDOR_ID);
                if (vendor == 0xFFFF || vendor == 0) continue;
                if (pci_read_config_byte((unsigned char)bus, slot, func, PCI_CLASS) != wanted_class ||
                    pci_read_config_byte((unsigned char)bus, slot, func, PCI_SUBCLASS) != wanted_subclass ||
                    pci_read_config_byte((unsigned char)bus, slot, func, PCI_PROG_IF) != wanted_prog_if)
                    continue;
                if (out_dev) {
                    out_dev->bus = (unsigned char)bus;
                    out_dev->slot = slot;
                    out_dev->func = func;
                    out_dev->vendor_id = vendor;
                    out_dev->device_id = pci_read_config_word((unsigned char)bus, slot, func,
                                                               PCI_DEVICE_ID);
                    out_dev->class_id = wanted_class;
                    out_dev->subclass_id = wanted_subclass;
                    out_dev->prog_if = wanted_prog_if;
                    out_dev->bar0 = pci_read_config_dword((unsigned char)bus, slot, func, PCI_BAR0);
                    out_dev->bar1 = pci_read_config_dword((unsigned char)bus, slot, func, PCI_BAR1);
                    out_dev->irq = pci_read_config_byte((unsigned char)bus, slot, func,
                                                        PCI_INTERRUPT_LINE);
                }
                return 1;
            }
        }
    }
    return 0;
}

// Scans PCI bus for modern audio controllers (Intel HDA, AC'97, Ensoniq, Sound Blaster PCI)
int pci_find_audio_device(pci_device_t* out_dev) {
    for (unsigned short bus = 0; bus < 8; bus++) {
        for (unsigned char slot = 0; slot < 32; slot++) {
            for (unsigned char func = 0; func < 8; func++) {
                unsigned short vendor = pci_read_config_word(bus, slot, func, PCI_VENDOR_ID);
                if (vendor == 0xFFFF || vendor == 0x0000) {
                    if (func == 0) break; // Slot empty
                    continue;
                }

                unsigned char class_id = pci_read_config_byte(bus, slot, func, PCI_CLASS);
                unsigned char subclass_id = pci_read_config_byte(bus, slot, func, PCI_SUBCLASS);

                // Class 0x04 = Multimedia device (Audio: 0x01 or 0x03 for Intel HDA)
                if (class_id == PCI_CLASS_MULTIMEDIA &&
                    (subclass_id == PCI_SUBCLASS_AUDIO || subclass_id == PCI_SUBCLASS_HDA)) {
                    if (out_dev) {
                        out_dev->bus = bus;
                        out_dev->slot = slot;
                        out_dev->func = func;
                        out_dev->vendor_id = vendor;
                        out_dev->device_id = pci_read_config_word(bus, slot, func, PCI_DEVICE_ID);
                        out_dev->class_id = class_id;
                        out_dev->subclass_id = subclass_id;
                        out_dev->prog_if = pci_read_config_byte(bus, slot, func, PCI_PROG_IF);
                        out_dev->bar0 = pci_read_config_dword(bus, slot, func, PCI_BAR0);
                        out_dev->bar1 = pci_read_config_dword(bus, slot, func, PCI_BAR1);
                        out_dev->irq = pci_read_config_byte(bus, slot, func, PCI_INTERRUPT_LINE);
                    }
                    return 1; // Found
                }
            }
        }
    }
    return 0; // Not found
}
