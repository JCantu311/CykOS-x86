#include "module.h"
#include "cykastd.h"

#define PIT_COMMAND  0x43
#define PIT_CHANNEL2 0x42
#define SPEAKER_PORT 0x61
#define PIT_CLOCK    1193182U

// Safe hardware verification:
// Reads back Port 0x61 to verify speaker timer gate circuit is present
static int detect_speaker_hardware(void) {
    unsigned char val = inb(SPEAKER_PORT);
    // If bus is unmapped, it floats to 0xFF; on real x86 hardware or VM, port 0x61 is always accessible
    return (val != 0xFF);
}

static void sound_tone(unsigned int frequency, unsigned int milliseconds) {
    if (frequency == 0 || milliseconds == 0) return;

    unsigned int divisor = PIT_CLOCK / frequency;
    if (divisor == 0 || divisor > 0xFFFF) divisor = 0xFFFF;

    // Set PIT Channel 2 to Mode 3 (Square Wave Generator), 16-bit access
    outb(PIT_COMMAND, 0xB6);
    outb(PIT_CHANNEL2, (unsigned char)(divisor & 0xFF));
    outb(PIT_CHANNEL2, (unsigned char)(divisor >> 8));

    // Turn ON: Enable Channel 2 gate (bit 0) and speaker output (bit 1)
    unsigned char prev = inb(SPEAKER_PORT);
    outb(SPEAKER_PORT, (unsigned char)(prev | 0x03));

    msecwait(milliseconds);

    // Turn OFF: Disable bits 0 and 1
    outb(SPEAKER_PORT, (unsigned char)(inb(SPEAKER_PORT) & 0xFC));
}

static void sound_chime(void) {
    sound_tone(784, 180);
    msecwait(60);
    sound_tone(988, 240);
}

static int sound_init(void) {
    if (!detect_speaker_hardware()) {
        print_color_string("[SOUND] Warning: Audio port 0x61 not responding.\n", VGA_RED);
        return 0;
    }

    print_color_string("[SOUND] Audio driver initialized.\n", VGA_GREEN);
    sound_chime();
    return 0;
}

__attribute__((section(".header"), used))
const cyk_sound_driver_t cyk_sound_driver = {
    .header = {
        .magic = CYKOS_MODULE_MAGIC,
        .name = "cysound",
        .version = 0x00010000,
        .type = MODULE_TYPE_SOUND,
        .load_address = MODULE_LOAD_ADDRESS,
        .init = sound_init,
    },
    .tone = sound_tone,
    .chime = sound_chime,
};
