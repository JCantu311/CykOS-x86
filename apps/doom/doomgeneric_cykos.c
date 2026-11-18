/* CykOS platform layer for DoomGeneric. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "doomgeneric.h"
#include "doomkeys.h"
#include "i_video.h"

#define SYS_MSLEEP       186
#define SYS_GFX_MODE     187
#define SYS_GFX_BITMAP   197
#define SYS_TRY_KEY_EVENT 200
#define SYS_GET_TICKS_MS 201
#define SYS_GFX_PALETTE  199
#define GFX_MODE_320X200   2

static int palette_needs_upload = 1;
static int input_debug_enabled;
static unsigned int input_debug_events;
static unsigned int input_debug_empty_polls;
static unsigned char g_physical_keys[256];

static void doom_log_key(const char *state, unsigned char key)
{
    if (input_debug_enabled && input_debug_events < 64) {
        printf("[DOOM-KEY] %s %u\n", state, (unsigned int)key);
        input_debug_events++;
    }
}

static inline uint32_t cykos_syscall1(uint32_t number, uint32_t a)
{
    register uint32_t eax __asm__("eax") = number;
    register uint32_t ebx __asm__("ebx") = a;
    __asm__ volatile("int $0x80" : "+a"(eax) : "b"(ebx) : "memory", "cc");
    return eax;
}

static inline uint32_t cykos_syscall0(uint32_t number)
{
    register uint32_t eax __asm__("eax") = number;
    __asm__ volatile("int $0x80" : "+a"(eax) : : "memory", "cc");
    return eax;
}

static inline uint32_t cykos_syscall2(uint32_t number, uint32_t a, uint32_t b)
{
    register uint32_t eax __asm__("eax") = number;
    register uint32_t ebx __asm__("ebx") = a;
    register uint32_t ecx __asm__("ecx") = b;
    __asm__ volatile("int $0x80" : "+a"(eax) : "b"(ebx), "c"(ecx) : "memory", "cc");
    return eax;
}

static inline uint32_t cykos_syscall3(uint32_t number, uint32_t a, uint32_t b,
                                     uint32_t c)
{
    register uint32_t eax __asm__("eax") = number;
    register uint32_t ebx __asm__("ebx") = a;
    register uint32_t ecx __asm__("ecx") = b;
    register uint32_t edx __asm__("edx") = c;
    __asm__ volatile("int $0x80" : "+a"(eax) : "b"(ebx), "c"(ecx), "d"(edx)
                     : "memory", "cc");
    return eax;
}

static void cykos_upload_doom_palette(void)
{
    unsigned char rgb[256 * 3];
    for (unsigned int i = 0; i < 256; ++i) {
        rgb[i * 3 + 0] = (unsigned char)colors[i].r;
        rgb[i * 3 + 1] = (unsigned char)colors[i].g;
        rgb[i * 3 + 2] = (unsigned char)colors[i].b;
    }
    // SYS_GFX_PALETTE packs first index in the upper 16 bits and count in
    // the lower 16 bits; VGA's DAC then displays Doom's own palette indices.
    if ((int)cykos_syscall2(SYS_GFX_PALETTE, 256, (uint32_t)rgb) == 0) {
        palette_needs_upload = 0;
        palette_changed = 0;
    }
}

void DG_Init(void)
{
    printf("[DOOM] Requesting VGA 320x200x256 mode.\n");
    int result = (int)cykos_syscall1(SYS_GFX_MODE, GFX_MODE_320X200);
    if (result != 0) {
        fprintf(stderr, "[DOOM] VGA mode setup failed (status %d).\n", result);
        exit(1);
    }
    printf("[DOOM] VGA mode active.\n");
}

void DG_DrawFrame(void)
{
    if (palette_needs_upload || palette_changed)
        cykos_upload_doom_palette();

    /* Keep the VGA address in the kernel. The bitmap syscall validates and
       copies this bounded app buffer through the display driver. */
    struct cyk_bitmap {
        uint32_t width;
        uint32_t height;
        uint32_t stride;
        uint32_t format;
        const unsigned char *pixels;
    } frame = {
        DOOMGENERIC_RESX, DOOMGENERIC_RESY, DOOMGENERIC_RESX, 1,
        (const unsigned char *)DG_ScreenBuffer
    };
    (void)cykos_syscall3(SYS_GFX_BITMAP, 0, (uint32_t)&frame, 0);
}

void DG_SleepMs(uint32_t ms)
{
    if (ms == 0) return;
    (void)cykos_syscall1(SYS_MSLEEP, ms);
}

uint32_t DG_GetTicksMs(void)
{
    return cykos_syscall0(SYS_GET_TICKS_MS);
}

static unsigned char cykos_scancode_to_doom(unsigned int scan)
{
    unsigned int code = scan & 0x7Fu;
    int extended = scan >= 128;

    if (extended) {
        switch (code) {
            case 0x48: return KEY_UPARROW;
            case 0x50: return KEY_DOWNARROW;
            case 0x4B: return KEY_LEFTARROW;
            case 0x4D: return KEY_RIGHTARROW;
            case 0x1D: return KEY_FIRE; /* right Ctrl */
            case 0x38: return KEY_LALT;
            default: return 0;
        }
    }

    switch (code) {
        case 0x01: return KEY_ESCAPE;
        case 0x0E: return KEY_BACKSPACE;
        case 0x0F: return KEY_TAB;
        case 0x1C: return KEY_ENTER;
        case 0x1D: return KEY_FIRE; /* left Ctrl */
        case 0x2A: case 0x36: return KEY_RSHIFT;
        case 0x38: return KEY_LALT;
        case 0x39: return KEY_USE;
        case 0x48: return KEY_UPARROW;
        case 0x50: return KEY_DOWNARROW;
        case 0x4B: return KEY_LEFTARROW;
        case 0x4D: return KEY_RIGHTARROW;
        case 0x47: return KEY_HOME;
        case 0x4F: return KEY_END;
        case 0x49: return KEY_PGUP;
        case 0x51: return KEY_PGDN;
        case 0x52: return KEY_INS;
        case 0x53: return KEY_DEL;
        case 0x3B: return KEY_F1;
        case 0x3C: return KEY_F2;
        case 0x3D: return KEY_F3;
        case 0x3E: return KEY_F4;
        case 0x3F: return KEY_F5;
        case 0x40: return KEY_F6;
        case 0x41: return KEY_F7;
        case 0x42: return KEY_F8;
        case 0x43: return KEY_F9;
        case 0x44: return KEY_F10;
        case 0x57: return KEY_F11;
        case 0x58: return KEY_F12;
        default: break;
    }

    static const unsigned char set1_ascii[128] = {
        0, 27, '1','2','3','4','5','6','7','8','9','0','-','=','\b','\t',
        'q','w','e','r','t','y','u','i','o','p','[',']','\n',0,'a','s',
        'd','f','g','h','j','k','l',';','\'','`',0,'\\','z','x','c','v',
        'b','n','m',',','.','/',0,'*',0,' ',0,
    };
    return code < sizeof(set1_ascii) ? set1_ascii[code] : 0;
}

int DG_GetKey(int *pressed, unsigned char *key)
{
    for (;;) {
        int value = (int)cykos_syscall0(SYS_TRY_KEY_EVENT);
        if (value == -1) {
            if (input_debug_enabled && input_debug_empty_polls < 4) {
                printf("[DOOM-RAW] queue empty\n");
                input_debug_empty_polls++;
            }
            return 0; /* only an empty queue ends the poll */
        }
        if (input_debug_enabled && input_debug_events < 64) {
            printf("[DOOM-RAW] %d\n", value);
            input_debug_events++;
        }

        unsigned int key_id;
        unsigned int scan;
        if (value >= 0x101 && value <= 0x200) {
            key_id = (unsigned int)value - 0x100u;
            scan = key_id - 1u;
            if (scan >= 256 || !g_physical_keys[scan]) continue;
            g_physical_keys[scan] = 0;
            *key = cykos_scancode_to_doom(scan);
            if (*key == 0) continue;
            *pressed = 0;
            doom_log_key("up", *key);
            return 1;
        }
        if (value < 1 || value > 256) continue;

        key_id = (unsigned int)value;
        scan = key_id - 1u;
        if (g_physical_keys[scan]) continue; /* hardware typematic repeat */
        g_physical_keys[scan] = 1;
        *key = cykos_scancode_to_doom(scan);
        if (*key == 0) continue;
        *pressed = 1;
        doom_log_key("down", *key);
        return 1;
    }
}

void DG_SetWindowTitle(const char *title)
{
    (void)title;
}

int main(int argc, char **argv)
{
    char *game_argv[18];
    int has_iwad = 0;
    int game_argc = 0;

    if (argc > 16) argc = 16;
    for (int i = 0; i < argc; ++i) {
        if (strcmp(argv[i], "-inputdebug") == 0) {
            input_debug_enabled = 1;
            continue;
        }
        game_argv[game_argc++] = argv[i];
        if (strcmp(argv[i], "-iwad") == 0) has_iwad = 1;
    }

    if (!has_iwad && game_argc <= 15) {
        game_argv[game_argc++] = "-iwad";
        game_argv[game_argc++] = "/USR/SHARE/GAMES/DOOM/DOOM1.WAD";
    }
    game_argv[game_argc] = NULL;

    printf("[DOOM] Starting DoomGeneric initialization.\n");
    doomgeneric_Create(game_argc, game_argv);
    printf("[DOOM] Initialization complete; entering game loop.\n");
    uint32_t deadline = DG_GetTicksMs();
    uint32_t frame_remainder = 0;
    for (;;) {
        doomgeneric_Tick();
        deadline += 1000u / 35u;
        frame_remainder += 1000u % 35u;
        if (frame_remainder >= 35u) {
            deadline++;
            frame_remainder -= 35u;
        }
        uint32_t now = DG_GetTicksMs();
        int32_t remaining = (int32_t)(deadline - now);
        if (remaining > 0) {
            DG_SleepMs((uint32_t)remaining);
        } else {
            /* Drop late frames instead of running a catch-up burst. */
            deadline = now;
            frame_remainder = 0;
        }
    }
    return 0;
}
