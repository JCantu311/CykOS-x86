#include "module.h"
#include "cykastd.h"

// VGA Hardware Ports
#define VGA_AC_INDEX        0x3C0
#define VGA_AC_WRITE        0x3C0
#define VGA_AC_READ         0x3C1
#define VGA_MISC_WRITE      0x3C2
#define VGA_SEQ_INDEX       0x3C4
#define VGA_SEQ_DATA        0x3C5
#define VGA_DAC_READ_INDEX  0x3C7
#define VGA_DAC_WRITE_INDEX 0x3C8
#define VGA_DAC_DATA        0x3C9
#define VGA_GC_INDEX        0x3CE
#define VGA_GC_DATA         0x3CF
#define VGA_CRTC_INDEX      0x3D4
#define VGA_CRTC_DATA       0x3D5
#define VGA_INSTAT_READ     0x3DA

#define VGA_GFX_ADDR        ((unsigned char*)0xA0000)
#define GFX_WIDTH           320
#define GFX_HEIGHT          200

static int g_current_mode = 0; // 0 = 80x25 text, 1 = 320x200 graphics

// Mode 13h (320x200 256 colors) register table
static const unsigned char g_mode13_regs[] = {
    // MISC (1 byte)
    0x63,
    // SEQUENCER (5 bytes: 0x00..0x04)
    0x03, 0x01, 0x0F, 0x00, 0x0E,
    // CRTC (25 bytes: 0x00..0x18)
    0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F,
    0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x9C, 0x0E, 0x8F, 0x28, 0x40, 0x96, 0xB9, 0xA3, 0xFF,
    // GRAPHICS CONTROLLER (9 bytes: 0x00..0x08)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0F, 0xFF,
    // ATTRIBUTE CONTROLLER (21 bytes: 0x00..0x14)
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x41, 0x00, 0x0F, 0x00, 0x00
};

// Mode 03h (80x25 16-color text) register table
static const unsigned char g_mode3_regs[] = {
    // MISC (1 byte)
    0x67,
    // SEQUENCER (5 bytes)
    0x03, 0x00, 0x03, 0x00, 0x02,
    // CRTC (25 bytes)
    0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F,
    0x00, 0x4F, 0x0E, 0x0F, 0x00, 0x00, 0x00, 0x00,
    0x9C, 0x0E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3, 0xFF,
    // GRAPHICS CONTROLLER (9 bytes)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0E, 0x00, 0xFF,
    // ATTRIBUTE CONTROLLER (21 bytes)
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
    0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
    0x0C, 0x00, 0x0F, 0x08, 0x00
};

static void write_vga_registers(const unsigned char* regs) {
    // Write MISC
    outb(VGA_MISC_WRITE, *regs++);

    // Write SEQUENCER
    for (unsigned char i = 0; i < 5; i++) {
        outb(VGA_SEQ_INDEX, i);
        outb(VGA_SEQ_DATA, *regs++);
    }

    // Unlock CRTC registers 0..7
    outb(VGA_CRTC_INDEX, 0x03);
    outb(VGA_CRTC_DATA, inb(VGA_CRTC_DATA) | 0x80);
    outb(VGA_CRTC_INDEX, 0x11);
    outb(VGA_CRTC_DATA, inb(VGA_CRTC_DATA) & ~0x80);

    // Write CRTC
    for (unsigned char i = 0; i < 25; i++) {
        outb(VGA_CRTC_INDEX, i);
        outb(VGA_CRTC_DATA, *regs++);
    }

    // Write GRAPHICS CONTROLLER
    for (unsigned char i = 0; i < 9; i++) {
        outb(VGA_GC_INDEX, i);
        outb(VGA_GC_DATA, *regs++);
    }

    // Write ATTRIBUTE CONTROLLER
    for (unsigned char i = 0; i < 21; i++) {
        inb(VGA_INSTAT_READ); // Reset flip-flop
        outb(VGA_AC_INDEX, i);
        outb(VGA_AC_WRITE, *regs++);
    }

    // Re-enable video display
    inb(VGA_INSTAT_READ);
    outb(VGA_AC_INDEX, 0x20);
}

static void gfx_clear(unsigned char color) {
    if (g_current_mode == 1) {
        memset(VGA_GFX_ADDR, color, GFX_WIDTH * GFX_HEIGHT);
    } else {
        clear_screen();
    }
}

static int gfx_set_mode(int mode) {
    if (mode == 1) {
        // Mode 13h: 320x200 256 colors
        write_vga_registers(g_mode13_regs);
        g_current_mode = 1;
        gfx_clear(0);
        return 0;
    } else if (mode == 0) {
        // Mode 03h: 80x25 text
        write_vga_registers(g_mode3_regs);
        g_current_mode = 0;
        clear_screen();
        enable_hardware_cursor();
        return 0;
    }
    return -1;
}

static int gfx_get_mode(void) {
    return g_current_mode;
}

static void gfx_put_pixel(int x, int y, unsigned char color) {
    if (g_current_mode != 1) return;
    if (x >= 0 && x < GFX_WIDTH && y >= 0 && y < GFX_HEIGHT) {
        VGA_GFX_ADDR[y * GFX_WIDTH + x] = color;
    }
}

static void gfx_fill_rect(int x, int y, int w, int h, unsigned char color) {
    if (g_current_mode != 1) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > GFX_WIDTH) w = GFX_WIDTH - x;
    if (y + h > GFX_HEIGHT) h = GFX_HEIGHT - y;
    if (w <= 0 || h <= 0) return;

    for (int r = 0; r < h; r++) {
        memset(&VGA_GFX_ADDR[(y + r) * GFX_WIDTH + x], color, w);
    }
}

static void gfx_draw_bitmap(int x, int y, int w, int h, const unsigned char* data) {
    if (g_current_mode != 1 || !data) return;
    for (int r = 0; r < h; r++) {
        int dst_y = y + r;
        if (dst_y < 0 || dst_y >= GFX_HEIGHT) continue;
        for (int c = 0; c < w; c++) {
            int dst_x = x + c;
            if (dst_x < 0 || dst_x >= GFX_WIDTH) continue;
            unsigned char pixel = data[r * w + c];
            if (pixel != 0xFF) { // 0xFF = transparent color key
                VGA_GFX_ADDR[dst_y * GFX_WIDTH + dst_x] = pixel;
            }
        }
    }
}

static unsigned char* gfx_get_framebuffer(void) {
    return (g_current_mode == 1) ? VGA_GFX_ADDR : (unsigned char*)0xB8000;
}

static int gfx_init(void) {
    g_current_mode = 0; // Boot starts in text mode
    return 0;
}

__attribute__((section(".header"), used))
const cyk_display_driver_t cyk_display_driver = {
    .header = {
        .magic = CYKOS_MODULE_MAGIC,
        .name = "cydisplay",
        .version = 0x00010000,
        .type = MODULE_TYPE_DISPLAY,
        .load_address = MODULE_LOAD_ADDRESS,
        .init = gfx_init,
    },
    .set_mode = gfx_set_mode,
    .get_mode = gfx_get_mode,
    .put_pixel = gfx_put_pixel,
    .fill_rect = gfx_fill_rect,
    .clear = gfx_clear,
    .draw_bitmap = gfx_draw_bitmap,
    .get_framebuffer = gfx_get_framebuffer,
};
