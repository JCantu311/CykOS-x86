#include "module.h"
#include "cykastd.h"

// Intel HDA / HD Audio detection driver.
// Performs PCI config reads for HDA controller detection.
// NOTE: PCI scan is deferred to init() time to avoid boot-loop issues from
//       early hardware initialization. The scan only runs when explicitly
//       triggered by the kernel after PCI bus enumeration is complete.

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC
#define PCI_VENDOR_ID      0x00
#define PCI_DEVICE_ID      0x02
#define PCI_CLASS          0x0B
#define PCI_SUBCLASS       0x0A
#define PCI_BAR0           0x10
#define PCI_INTERRUPT_LINE 0x3C
#define PCI_CLASS_MULTIMEDIA 0x04
#define PCI_SUBCLASS_AUDIO   0x01
#define PCI_SUBCLASS_HDA     0x03

typedef struct {
    unsigned char bus, slot, func;
    unsigned short vendor_id, device_id;
    unsigned char class_id, subclass_id, prog_if;
    unsigned int bar0;
    unsigned char irq;
} hda_pci_device_t;

static unsigned char hda_inb_cfg(unsigned char bus, unsigned char slot,
                                  unsigned char func, unsigned char offset) {
    unsigned int addr = (bus << 16) | (slot << 11) | (func << 8) |
                        (offset & 0xFC) | 0x80000000u;
    outb((unsigned short)(PCI_CONFIG_ADDRESS + 0), (unsigned char)(addr & 0xFF));
    outb((unsigned short)(PCI_CONFIG_ADDRESS + 1), (unsigned char)((addr >> 8) & 0xFF));
    outb((unsigned short)(PCI_CONFIG_ADDRESS + 2), (unsigned char)((addr >> 16) & 0xFF));
    outb((unsigned short)(PCI_CONFIG_ADDRESS + 3), (unsigned char)((addr >> 24) & 0xFF));
    return inb((unsigned short)(PCI_CONFIG_DATA + (offset & 3)));
}

static unsigned short hda_inw_cfg(unsigned char bus, unsigned char slot,
                                   unsigned char func, unsigned char offset) {
    unsigned int addr = (bus << 16) | (slot << 11) | (func << 8) |
                        (offset & 0xFC) | 0x80000000u;
    outb((unsigned short)(PCI_CONFIG_ADDRESS + 0), (unsigned char)(addr & 0xFF));
    outb((unsigned short)(PCI_CONFIG_ADDRESS + 1), (unsigned char)((addr >> 8) & 0xFF));
    outb((unsigned short)(PCI_CONFIG_ADDRESS + 2), (unsigned char)((addr >> 16) & 0xFF));
    outb((unsigned short)(PCI_CONFIG_ADDRESS + 3), (unsigned char)((addr >> 24) & 0xFF));
    unsigned int d = (unsigned int)inb((unsigned short)(PCI_CONFIG_DATA + 0))
                   | ((unsigned int)inb((unsigned short)(PCI_CONFIG_DATA + 1)) << 8)
                   | ((unsigned int)inb((unsigned short)(PCI_CONFIG_DATA + 2)) << 16)
                   | ((unsigned int)inb((unsigned short)(PCI_CONFIG_DATA + 3)) << 24);
    return (unsigned short)((d >> ((offset & 2) * 8)) & 0xFFFFu);
}

static unsigned int hda_inl_cfg(unsigned char bus, unsigned char slot,
                                 unsigned char func, unsigned char offset) {
    unsigned int addr = (bus << 16) | (slot << 11) | (func << 8) |
                        (offset & 0xFC) | 0x80000000u;
    outb((unsigned short)(PCI_CONFIG_ADDRESS + 0), (unsigned char)(addr & 0xFF));
    outb((unsigned short)(PCI_CONFIG_ADDRESS + 1), (unsigned char)((addr >> 8) & 0xFF));
    outb((unsigned short)(PCI_CONFIG_ADDRESS + 2), (unsigned char)((addr >> 16) & 0xFF));
    outb((unsigned short)(PCI_CONFIG_ADDRESS + 3), (unsigned char)((addr >> 24) & 0xFF));
    return (unsigned int)inb((unsigned short)(PCI_CONFIG_DATA + 0))
         | ((unsigned int)inb((unsigned short)(PCI_CONFIG_DATA + 1)) << 8)
         | ((unsigned int)inb((unsigned short)(PCI_CONFIG_DATA + 2)) << 16)
         | ((unsigned int)inb((unsigned short)(PCI_CONFIG_DATA + 3)) << 24);
}

// ---------------------------------------------------------------------------
// Driver state
// ---------------------------------------------------------------------------

static hda_pci_device_t g_hda_dev;
static int g_hda_found = 0;

static int hdasound_init(void) {
    g_hda_found = 0;

    for (unsigned short bus = 0; bus < 8 && !g_hda_found; bus++) {
        for (unsigned char slot = 0; slot < 32 && !g_hda_found; slot++) {
            for (unsigned char func = 0; func < 8 && !g_hda_found; func++) {
                unsigned short vendor = hda_inw_cfg(bus, slot, func, PCI_VENDOR_ID);
                if (vendor == 0xFFFFu || vendor == 0x0000u) {
                    if (func == 0) break;
                    continue;
                }

                unsigned char class_id = hda_inb_cfg(bus, slot, func, PCI_CLASS);
                unsigned char subclass_id = hda_inb_cfg(bus, slot, func, PCI_SUBCLASS);

                if (class_id == PCI_CLASS_MULTIMEDIA &&
                    (subclass_id == PCI_SUBCLASS_AUDIO ||
                     subclass_id == PCI_SUBCLASS_HDA)) {
                    g_hda_dev.bus = bus;
                    g_hda_dev.slot = slot;
                    g_hda_dev.func = func;
                    g_hda_dev.vendor_id = vendor;
                    g_hda_dev.device_id = hda_inw_cfg(bus, slot, func, PCI_DEVICE_ID);
                    g_hda_dev.class_id = class_id;
                    g_hda_dev.subclass_id = subclass_id;
                    g_hda_dev.prog_if = hda_inb_cfg(bus, slot, func, 0x09);
                    g_hda_dev.bar0 = hda_inl_cfg(bus, slot, func, PCI_BAR0);
                    g_hda_dev.irq = hda_inb_cfg(bus, slot, func, PCI_INTERRUPT_LINE);
                    g_hda_found = 1;
                }
            }
        }
    }

    if (g_hda_found) {
        print_color_string("[SOUND] HDA audio controller detected (PCI ",
                           VGA_GREEN);
        {
            unsigned char b = g_hda_dev.bus;
            char hi = (b >> 4) & 0xF;
            char lo = b & 0xF;
            print_char((hi < 10) ? (char)('0' + hi) : (char)('A' + hi - 10));
            print_char((lo < 10) ? (char)('0' + lo) : (char)('A' + lo - 10));
        }
        print_char(':');
        {
            unsigned char s = g_hda_dev.slot;
            char hi = (s >> 4) & 0xF;
            char lo = s & 0xF;
            print_char((hi < 10) ? (char)('0' + hi) : (char)('A' + hi - 10));
            print_char((lo < 10) ? (char)('0' + lo) : (char)('A' + lo - 10));
        }
        print_char(':');
        {
            unsigned char f = g_hda_dev.func;
            char hi = (f >> 4) & 0xF;
            char lo = f & 0xF;
            print_char((hi < 10) ? (char)('0' + hi) : (char)('A' + hi - 10));
            print_char((lo < 10) ? (char)('0' + lo) : (char)('A' + lo - 10));
        }
        print_char(')');
        print_char('\n');
        print_color_string("[SOUND] HDA playback not yet implemented; using PC speaker fallback.\n",
                           VGA_YELLOW);
    } else {
        print_color_string("[SOUND] No HDA audio controller found on PCI bus.\n", VGA_WHITE);
    }
    return 0;  // Always succeed (detection is non-fatal)
}

static void hdasound_tone(unsigned int frequency, unsigned int milliseconds) {
    // HDA driver cannot produce PC-speaker tones; return silently.
    (void)frequency; (void)milliseconds;
}

static void hdasound_chime(void) {
    // No PC speaker on HDA path.
}

static int hdasound_set_volume(unsigned int volume) {
    (void)volume;
    return -1;
}

static int hdasound_play_sample(const unsigned char* data, unsigned int len,
                                 unsigned int freq_hz) {
    // Full HDA sample playback not yet implemented.
    (void)data; (void)len; (void)freq_hz;
    return -1;
}

static int hdasound_stop(void) {
    return 0;
}

static int hdasound_get_caps(void) {
    // HDA hardware is detected but no playback driver is implemented yet.
    // Return SOUND_CAP_NONE so the sound subsystem falls back to PC speaker.
    return SOUND_CAP_NONE;
}

// ---------------------------------------------------------------------------
// Module header
// ---------------------------------------------------------------------------

__attribute__((section(".header"), used))
const cyk_sound_driver_t cyk_hdasound_driver = {
    .header = {
        .magic = CYKOS_MODULE_MAGIC,
        .name = "hdasound",
        .version = 0x00010000,
        .type = MODULE_TYPE_SOUND,
        .load_address = 0x54000,  // Module slot 4 (after pcsound at 0x50000)
        .init = hdasound_init,
    },
    .tone = hdasound_tone,
    .chime = hdasound_chime,
    .set_volume = hdasound_set_volume,
    .play_sample = hdasound_play_sample,
    .stop = hdasound_stop,
    .get_caps = hdasound_get_caps,
    .hardware_name = "Intel HDA Audio",
};