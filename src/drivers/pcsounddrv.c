#include "module.h"
#include "cykastd.h"

#define PIT_COMMAND  0x43
#define PIT_CHANNEL2 0x42
#define SPEAKER_PORT 0x61
#define PIT_CLOCK    1193182U

// Audible PC-speaker range. Divisor = PIT_CLOCK / frequency.
#define MIN_FREQ_HZ  20u
#define MAX_FREQ_HZ  20000u

static unsigned int g_pcsound_volume = 70;  // Default volume (0-100)

// ---------------------------------------------------------------------------
// PC Speaker hardware access
// ---------------------------------------------------------------------------

static int pcspeaker_present(void) {
    // Sample port 0x61 multiple times; on x86 chipsets and PC VMs it is
    // always readable. A sustained 0xFF indicates an unmapped bus.
    for (int i = 0; i < 4; i++) {
        unsigned char val = inb(SPEAKER_PORT);
        if (val != 0xFF) return 1;
    }
    return 0;
}

static void speaker_enable(void) {
    unsigned char prev = inb(SPEAKER_PORT);
    outb(SPEAKER_PORT, (unsigned char)(prev | 0x03));  // bit 0: timer-2 gate, bit 1: speaker data
}

static void speaker_disable(void) {
    outb(SPEAKER_PORT, (unsigned char)(inb(SPEAKER_PORT) & 0xFC));  // clear gate + speaker data
}

// ---------------------------------------------------------------------------
// Standardized sound driver API
// ---------------------------------------------------------------------------

static void pcsound_tone(unsigned int frequency, unsigned int milliseconds) {
    if (frequency == 0 || milliseconds == 0) return;

    // Clamp into the audible / representable range.
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

static void pcsound_chime(void) {
    pcsound_tone(784, 180);   // G5
    msecwait(60);
    pcsound_tone(988, 240);   // B5
}

static int pcsound_set_volume(unsigned int volume) {
    // PC speaker has no hardware volume control; accept the value for API
    // compatibility but cannot attenuate the square wave.
    if (volume > 100) volume = 100;
    g_pcsound_volume = volume;
    return 0;
}

static int pcsound_play_sample(const unsigned char* data, unsigned int len,
                                unsigned int freq_hz) {
    // PC speaker cannot play digitized samples. Return error so callers
    // know sample playback is unsupported on this hardware.
    (void)data; (void)len; (void)freq_hz;
    return -1;
}

static int pcsound_stop(void) {
    speaker_disable();
    return 0;
}

static int pcsound_get_caps(void) {
    return SOUND_CAP_TONE | SOUND_CAP_CHIME | SOUND_CAP_VOLUME;
}

// ---------------------------------------------------------------------------
// Driver initialization
// ---------------------------------------------------------------------------

static int pcsound_init(void) {
    // Always leave the speaker off at init time.
    speaker_disable();

    if (!pcspeaker_present()) {
        print_color_string("[SOUND] PC speaker: port 0x61 not responding.\n", VGA_RED);
        return -1;  // Hardware not present
    }

    print_color_string("[SOUND] PC speaker driver initialized.\n", VGA_GREEN);
    return 0;
}

// ---------------------------------------------------------------------------
// Module header
// ---------------------------------------------------------------------------

__attribute__((section(".header"), used))
const cyk_sound_driver_t cyk_pcsound_driver = {
    .header = {
        .magic = CYKOS_MODULE_MAGIC,
        .name = "pcsound",
        .version = 0x00010000,
        .type = MODULE_TYPE_SOUND,
        .load_address = MODULE_LOAD_ADDRESS,
        .init = pcsound_init,
    },
    .tone = pcsound_tone,
    .chime = pcsound_chime,
    .set_volume = pcsound_set_volume,
    .play_sample = pcsound_play_sample,
    .stop = pcsound_stop,
    .get_caps = pcsound_get_caps,
    .hardware_name = "PC Speaker",
};