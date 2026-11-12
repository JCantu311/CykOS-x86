#include "module.h"
#include "cykastd.h"

#define PIT_COMMAND 0x43
#define PIT_CHANNEL2 0x42
#define SPEAKER_PORT 0x61
#define PIT_CLOCK 1193182U

static void sound_tone(unsigned int frequency, unsigned int milliseconds) {
    if (frequency == 0) return;

    unsigned int divisor = PIT_CLOCK / frequency;
    if (divisor == 0 || divisor > 0xFFFF) divisor = 0xFFFF;

    outb(PIT_COMMAND, 0xB6);
    outb(PIT_CHANNEL2, (unsigned char)(divisor & 0xFF));
    outb(PIT_CHANNEL2, (unsigned char)(divisor >> 8));
    outb(SPEAKER_PORT, (unsigned char)(inb(SPEAKER_PORT) | 0x03));

    unsigned int rounds = milliseconds * 1200;
    while (rounds--) {
        asm volatile("pause");
    }

    outb(SPEAKER_PORT, (unsigned char)(inb(SPEAKER_PORT) & 0xFC));
}

static void sound_chime(void) {
    sound_tone(784, 90);
    sound_tone(988, 120);
}

static int sound_init(void) {
    sound_chime();
    return 0;
}

__attribute__((section(".header"), used))
const cyk_module_header_t cyk_sound_driver = {
    .magic = CYKOS_MODULE_MAGIC,
    .name = "cysound",
    .version = 0x00010000,
    .type = MODULE_TYPE_SOUND,
    .load_address = MODULE_LOAD_ADDRESS,
    .init = sound_init,
};
