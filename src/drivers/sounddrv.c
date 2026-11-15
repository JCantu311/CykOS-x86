#include "module.h"
#include "cykastd.h"

#define PIT_COMMAND  0x43
#define PIT_CHANNEL2 0x42
#define SPEAKER_PORT 0x61
#define PIT_CLOCK    1193182U

// Audible PC-speaker range. Divisor = PIT_CLOCK / frequency.
// 20 Hz  -> divisor 59659 (representable)   20 kHz -> divisor ~60 (representable)
#define MIN_FREQ_HZ  20u
#define MAX_FREQ_HZ  20000u

// Reads the system control port B (0x61). On x86 this port is always implemented
// by the chipset (and emulated by PC VMs), but a physical PC speaker may not be
// wired to its output on modern x64 hardware, in which case the driver loads
// cleanly yet produces no audible sound.
static int detect_speaker_hardware(void) {
    // Sample port 0x61 a few times: on real x86 chipsets and PC-emulating VMs
    // it is always readable. A sustained 0xFF would indicate an unmapped bus.
    unsigned char last_val = 0xFF;
    for (int i = 0; i < 4; i++) {
        unsigned char val = inb(SPEAKER_PORT);
        last_val = val;
        if (val != 0xFF) return 1;
    }
    print_color_string("[SOUND] Debug: port 0x61 read 0x", VGA_WHITE);
    // print hex nibble
    {
        unsigned char hi = (last_val >> 4) & 0xF;
        unsigned char lo = last_val & 0xF;
        char c = (hi < 10) ? (char)('0' + hi) : (char)('A' + hi - 10);
        print_char(c);
        c = (lo < 10) ? (char)('0' + lo) : (char)('A' + lo - 10);
        print_char(c);
    }
    print_char('\n');
    return 0;
}

static void speaker_enable(void) {
    unsigned char prev = inb(SPEAKER_PORT);
    outb(SPEAKER_PORT, (unsigned char)(prev | 0x03));  // bit 0: timer-2 gate, bit 1: speaker data
}

static void speaker_disable(void) {
    outb(SPEAKER_PORT, (unsigned char)(inb(SPEAKER_PORT) & 0xFC));  // clear gate + speaker data
}

static void sound_tone(unsigned int frequency, unsigned int milliseconds) {
    if (frequency == 0 || milliseconds == 0) return;

    // Clamp into the audible / representable range before computing the divisor.
    if (frequency < MIN_FREQ_HZ)  frequency = MIN_FREQ_HZ;
    if (frequency > MAX_FREQ_HZ)  frequency = MAX_FREQ_HZ;

    unsigned int divisor = PIT_CLOCK / frequency;
    if (divisor == 0)       divisor = 1;
    if (divisor > 0xFFFF)   divisor = 0xFFFF;

    // Program PIT channel 2: Mode 3 (square wave), 16-bit access, binary count
    outb(PIT_COMMAND, 0xB6);
    outb(PIT_CHANNEL2, (unsigned char)(divisor & 0xFF));
    outb(PIT_CHANNEL2, (unsigned char)(divisor >> 8));

    speaker_enable();
    msecwait(milliseconds);
    speaker_disable();
}

static void sound_chime(void) {
    sound_tone(784, 180);   // G5
    msecwait(60);
    sound_tone(988, 240);   // B5
}

static int sound_init(void) {
    // Always leave the speaker off at init time.
    speaker_disable();

    if (!detect_speaker_hardware()) {
        print_color_string("[SOUND] Warning: Audio port 0x61 not responding.\n", VGA_RED);
        return 0;
    }

    print_color_string("[SOUND] Audio driver initialized (PC speaker).\n", VGA_GREEN);
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
