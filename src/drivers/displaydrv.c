#include "module.h"
#include "cykastd.h"

#define VGA_AC_INDEX        0x3C0
#define VGA_MISC_WRITE      0x3C2
#define VGA_SEQ_INDEX       0x3C4
#define VGA_SEQ_DATA        0x3C5
#define VGA_GC_INDEX        0x3CE
#define VGA_GC_DATA         0x3CF
#define VGA_CRTC_INDEX      0x3D4
#define VGA_CRTC_DATA       0x3D5
#define VGA_INSTAT_READ     0x3DA
#define VGA_GFX_ADDR        ((volatile unsigned char*)0xA0000)

#define MODE_640_WIDTH      640
#define MODE_640_HEIGHT     480
#define MODE_640_PITCH      80
#define MODE_640_BYTES      (MODE_640_PITCH * MODE_640_HEIGHT)
#define MODE_320_WIDTH      320
#define MODE_320_HEIGHT     200
#define MODE_320_BYTES      (MODE_320_WIDTH * MODE_320_HEIGHT)
#define VBE_MODE_INFO       ((volatile unsigned char*)0x7200)
#define VBE_FB_ADDR         ((volatile unsigned char*)0x7228)

extern int vbe_set_display_mode(int mode);

static int g_current_mode;
static int g_cached_set_reset = -1;
static volatile unsigned char* g_linear_framebuffer;
static unsigned int g_linear_pitch;
static unsigned int g_linear_width;
static unsigned int g_linear_height;
static const unsigned char g_vga_palette[16][3] = {
    {0,0,0}, {0,0,170}, {0,170,0}, {0,170,170},
    {170,0,0}, {170,0,170}, {170,85,0}, {170,170,170},
    {85,85,85}, {85,85,255}, {85,255,85}, {85,255,255},
    {255,85,85}, {255,85,255}, {255,255,85}, {255,255,255}
};

// VGA BIOS compatible 640x480x16-color planar mode (Mode 12h).
static const unsigned char g_mode12_regs[] = {
    0xE3,
    0x03, 0x01, 0x0F, 0x00, 0x06,
    0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0x0B, 0x3E,
    0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xEA, 0x0C, 0xDF, 0x28, 0x00, 0xE7, 0x04, 0xE3, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x0F, 0xFF,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x01, 0x00, 0x0F, 0x00, 0x00
};

// VGA Mode 13h (320x200x256), retained for applications using a linear buffer.
static const unsigned char g_mode13_regs[] = {
    0x63,
    0x03, 0x01, 0x0F, 0x00, 0x0E,
    0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F,
    0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x9C, 0x0E, 0x8F, 0x28, 0x40, 0x96, 0xB9, 0xA3, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0F, 0xFF,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x41, 0x00, 0x0F, 0x00, 0x00
};

static const unsigned char g_mode3_regs[] = {
    0x67,
    0x03, 0x00, 0x03, 0x00, 0x02,
    0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F,
    0x00, 0x4F, 0x0E, 0x0F, 0x00, 0x00, 0x00, 0x00,
    0x9C, 0x0E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0E, 0x00, 0xFF,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
    0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
    0x0C, 0x00, 0x0F, 0x08, 0x00
};

static void write_vga_registers(const unsigned char* regs) {
    outb(VGA_MISC_WRITE, *regs++);
    for (unsigned char i = 0; i < 5; i++) {
        outb(VGA_SEQ_INDEX, i);
        outb(VGA_SEQ_DATA, *regs++);
    }
    outb(VGA_CRTC_INDEX, 0x03);
    outb(VGA_CRTC_DATA, inb(VGA_CRTC_DATA) | 0x80);
    outb(VGA_CRTC_INDEX, 0x11);
    outb(VGA_CRTC_DATA, inb(VGA_CRTC_DATA) & ~0x80);
    for (unsigned char i = 0; i < 25; i++) {
        outb(VGA_CRTC_INDEX, i);
        outb(VGA_CRTC_DATA, *regs++);
    }
    for (unsigned char i = 0; i < 9; i++) {
        outb(VGA_GC_INDEX, i);
        outb(VGA_GC_DATA, *regs++);
    }
    for (unsigned char i = 0; i < 21; i++) {
        inb(VGA_INSTAT_READ);
        outb(VGA_AC_INDEX, i);
        outb(VGA_AC_INDEX, *regs++);
    }
    inb(VGA_INSTAT_READ);
    outb(VGA_AC_INDEX, 0x20);
    g_cached_set_reset = -1;
}

static void planar_set_reset(unsigned char color) {
    color &= 0x0F;
    if (g_cached_set_reset != color) {
        outb(VGA_GC_INDEX, 0x00);
        outb(VGA_GC_DATA, color);
        outb(VGA_GC_INDEX, 0x01);
        outb(VGA_GC_DATA, 0x0F);
        outb(VGA_GC_INDEX, 0x03);
        outb(VGA_GC_DATA, 0x00);
        outb(VGA_SEQ_INDEX, 0x02);
        outb(VGA_SEQ_DATA, 0x0F);
        g_cached_set_reset = color;
    }
}

static void planar_write_masked(unsigned int offset, unsigned char mask) {
    volatile unsigned char latch;
    outb(VGA_GC_INDEX, 0x08);
    outb(VGA_GC_DATA, mask);
    latch = VGA_GFX_ADDR[offset];
    (void)latch;
    VGA_GFX_ADDR[offset] = 0xFF;
}

static void planar_put_pixel(int x, int y, unsigned char color) {
    unsigned int offset = (unsigned int)y * MODE_640_PITCH + ((unsigned int)x >> 3);
    unsigned char mask = (unsigned char)(0x80u >> (x & 7));
    planar_set_reset(color);
    planar_write_masked(offset, mask);
}

static unsigned char planar_get_pixel(int x, int y) {
    unsigned int offset = (unsigned int)y * MODE_640_PITCH + ((unsigned int)x >> 3);
    unsigned char mask = (unsigned char)(0x80u >> (x & 7));
    unsigned char color = 0;
    for (unsigned char plane = 0; plane < 4; plane++) {
        outb(VGA_GC_INDEX, 0x04);
        outb(VGA_GC_DATA, plane);
        if (VGA_GFX_ADDR[offset] & mask) color |= (unsigned char)(1u << plane);
    }
    return color;
}

static void planar_fill_rect(int x, int y, int w, int h, unsigned char color) {
    int y_end, x_end;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > MODE_640_WIDTH) w = MODE_640_WIDTH - x;
    if (y + h > MODE_640_HEIGHT) h = MODE_640_HEIGHT - y;
    if (w <= 0 || h <= 0) return;
    planar_set_reset(color);
    x_end = x + w - 1;
    y_end = y + h;
    for (int row = y; row < y_end; row++) {
        unsigned int first = (unsigned int)row * MODE_640_PITCH + ((unsigned int)x >> 3);
        unsigned int last = (unsigned int)row * MODE_640_PITCH + ((unsigned int)x_end >> 3);
        for (unsigned int byte = first; byte <= last; byte++) {
            int byte_x = (int)((byte - (unsigned int)row * MODE_640_PITCH) << 3);
            int left = x > byte_x ? x - byte_x : 0;
            int right = x_end < byte_x + 7 ? x_end - byte_x : 7;
            unsigned char mask = (unsigned char)((0xFFu >> left) & (0xFFu << (7 - right)));
            planar_write_masked(byte, mask);
        }
    }
    outb(VGA_GC_INDEX, 0x08);
    outb(VGA_GC_DATA, 0xFF);
}

static unsigned char rgb_to_vga16(unsigned char r, unsigned char g, unsigned char b) {
    unsigned int best_distance = 0xFFFFFFFFu;
    unsigned char best = 0;
    for (unsigned char i = 0; i < 16; i++) {
        int dr = (int)r - g_vga_palette[i][0];
        int dg = (int)g - g_vga_palette[i][1];
        int db = (int)b - g_vga_palette[i][2];
        unsigned int distance = (unsigned int)(dr * dr + dg * dg + db * db);
        if (distance < best_distance) {
            best_distance = distance;
            best = i;
        }
    }
    return best;
}

static unsigned char rgb_to_332(unsigned char r, unsigned char g, unsigned char b) {
    return (unsigned char)((r & 0xE0) | ((g >> 3) & 0x1C) | (b >> 6));
}

static int gfx_set_palette(unsigned int first, unsigned int count, const unsigned char* rgb8) {
    if ((g_current_mode != 1 && g_current_mode != 2) || !rgb8 ||
        count == 0 || first >= 256 || count > 256 - first) return -1;
    outb(0x3C8, (unsigned char)first);
    for (unsigned int i = 0; i < count * 3; i++)
        outb(0x3C9, (unsigned char)(((unsigned int)rgb8[i] * 63u + 127u) / 255u));
    return 0;
}

static void set_rgb332_palette(void) {
    outb(0x3C8, 0);
    for (unsigned int index = 0; index < 256; index++) {
        outb(0x3C9, (unsigned char)(((index >> 5) & 7) * 63 / 7));
        outb(0x3C9, (unsigned char)(((index >> 2) & 7) * 63 / 7));
        outb(0x3C9, (unsigned char)((index & 3) * 63 / 3));
    }
}

static unsigned char blend_channel(unsigned char source, unsigned char destination,
                                   unsigned char alpha) {
    return (unsigned char)(((unsigned int)source * alpha +
                            (unsigned int)destination * (255u - alpha)) / 255u);
}

static int bitmap_bytes_per_pixel(unsigned int format) {
    switch (format) {
        case CYK_BITMAP_INDEX8:
        case CYK_BITMAP_RGB332: return 1;
        case CYK_BITMAP_RGB24: return 3;
        case CYK_BITMAP_RGBA32: return 4;
        default: return 0;
    }
}

static void bitmap_pixel_rgb(const unsigned char* p, unsigned int format,
                             unsigned char* r, unsigned char* g, unsigned char* b,
                             int* transparent) {
    *transparent = 0;
    if (format == CYK_BITMAP_RGB332) {
        *r = (unsigned char)(((p[0] >> 5) & 7) * 255 / 7);
        *g = (unsigned char)(((p[0] >> 2) & 7) * 255 / 7);
        *b = (unsigned char)((p[0] & 3) * 255 / 3);
    } else if (format == CYK_BITMAP_RGB24 || format == CYK_BITMAP_RGBA32) {
        *r = p[0]; *g = p[1]; *b = p[2];
        if (format == CYK_BITMAP_RGBA32) {
            if (p[3] == 0) *transparent = 1;
        }
    } else {
        *r = *g = *b = 0;
    }
}

static int gfx_draw_bitmap_ex(int x, int y, const cyk_bitmap_t* bitmap) {
    int bytes_per_pixel;
    if ((g_current_mode != 1 && g_current_mode != 2 && g_current_mode != 3) ||
        !bitmap || !bitmap->pixels ||
        bitmap->width == 0 || bitmap->height == 0 ||
        bitmap->width > 4096 || bitmap->height > 4096) return -1;
    bytes_per_pixel = bitmap_bytes_per_pixel(bitmap->format);
    if (!bytes_per_pixel || bitmap->width > 0xFFFFFFFFu / (unsigned int)bytes_per_pixel ||
        bitmap->stride < bitmap->width * (unsigned int)bytes_per_pixel ||
        bitmap->height > 0xFFFFFFFFu / bitmap->stride) return -1;

    for (unsigned int row = 0; row < bitmap->height; row++) {
        int dst_y = y + (int)row;
        int target_width = g_current_mode == 3 ? MODE_640_WIDTH :
                           g_current_mode == 1 ? (int)g_linear_width : MODE_320_WIDTH;
        int target_height = g_current_mode == 3 ? MODE_640_HEIGHT :
                            g_current_mode == 1 ? (int)g_linear_height : MODE_320_HEIGHT;
        if (dst_y < 0 || dst_y >= target_height) continue;
        const unsigned char* src = bitmap->pixels + row * bitmap->stride;
        for (unsigned int col = 0; col < bitmap->width; col++) {
            int dst_x = x + (int)col;
            unsigned char color;
            if (dst_x < 0 || dst_x >= target_width) continue;
            if (bitmap->format == CYK_BITMAP_INDEX8) {
                if (src[col] == 0xFF) continue;
                color = src[col];
            } else {
                unsigned char r, g, b;
                int transparent;
                bitmap_pixel_rgb(src + col * bytes_per_pixel, bitmap->format,
                                 &r, &g, &b, &transparent);
                if (transparent) continue;
                if (bitmap->format == CYK_BITMAP_RGBA32 &&
                    src[col * bytes_per_pixel + 3] != 255) {
                    unsigned char alpha = src[col * bytes_per_pixel + 3];
                    unsigned char destination[3];
                    if (g_current_mode == 3) {
                        unsigned char old = planar_get_pixel(dst_x, dst_y);
                        destination[0] = g_vga_palette[old][0];
                        destination[1] = g_vga_palette[old][1];
                        destination[2] = g_vga_palette[old][2];
                    } else {
                        unsigned char old = g_current_mode == 1
                            ? g_linear_framebuffer[dst_y * g_linear_pitch + dst_x]
                            : VGA_GFX_ADDR[dst_y * MODE_320_WIDTH + dst_x];
                        destination[0] = (unsigned char)(((old >> 5) & 7) * 255 / 7);
                        destination[1] = (unsigned char)(((old >> 2) & 7) * 255 / 7);
                        destination[2] = (unsigned char)((old & 3) * 255 / 3);
                    }
                    r = blend_channel(src[col * bytes_per_pixel], destination[0], alpha);
                    g = blend_channel(src[col * bytes_per_pixel + 1], destination[1], alpha);
                    b = blend_channel(src[col * bytes_per_pixel + 2], destination[2], alpha);
                }
                color = g_current_mode == 3 ? rgb_to_vga16(r, g, b) : rgb_to_332(r, g, b);
            }
            if (g_current_mode == 3) planar_put_pixel(dst_x, dst_y, color);
            else if (g_current_mode == 1) g_linear_framebuffer[dst_y * g_linear_pitch + dst_x] = color;
            else VGA_GFX_ADDR[dst_y * MODE_320_WIDTH + dst_x] = color;
        }
    }
    outb(VGA_GC_INDEX, 0x08);
    outb(VGA_GC_DATA, 0xFF);
    return 0;
}

static void gfx_draw_bitmap(int x, int y, int w, int h, const unsigned char* data) {
    cyk_bitmap_t bitmap;
    if (w <= 0 || h <= 0 || !data) return;
    bitmap.width = (unsigned int)w;
    bitmap.height = (unsigned int)h;
    bitmap.stride = (unsigned int)w;
    bitmap.format = CYK_BITMAP_INDEX8;
    bitmap.pixels = data;
    (void)gfx_draw_bitmap_ex(x, y, &bitmap);
}

static void gfx_clear(unsigned char color) {
    if (g_current_mode == 1) {
        memset((void*)g_linear_framebuffer, color, g_linear_pitch * g_linear_height);
    } else if (g_current_mode == 3) {
        planar_set_reset(color);
        outb(VGA_GC_INDEX, 0x08);
        outb(VGA_GC_DATA, 0xFF);
        for (unsigned int i = 0; i < MODE_640_BYTES; i++) {
            volatile unsigned char latch = VGA_GFX_ADDR[i];
            (void)latch;
            VGA_GFX_ADDR[i] = 0xFF;
        }
    } else if (g_current_mode == 2) {
        memset((void*)VGA_GFX_ADDR, color, MODE_320_BYTES);
    } else {
        clear_screen();
    }
}

static int gfx_set_mode(int mode) {
    if (mode == 1) {
        if (vbe_set_display_mode(1)) {
            unsigned int fb = *(volatile unsigned int*)VBE_FB_ADDR;
            unsigned int pitch = *(volatile unsigned short*)(VBE_MODE_INFO + 16);
            unsigned int width = *(volatile unsigned short*)(VBE_MODE_INFO + 18);
            unsigned int height = *(volatile unsigned short*)(VBE_MODE_INFO + 20);
            if (fb && pitch >= width && width == MODE_640_WIDTH && height == MODE_640_HEIGHT) {
                g_linear_framebuffer = (volatile unsigned char*)fb;
                g_linear_pitch = pitch;
                g_linear_width = width;
                g_linear_height = height;
                g_current_mode = 1;
                set_rgb332_palette();
                gfx_clear(0);
                return 0;
            }
        }
        write_vga_registers(g_mode12_regs);
        g_current_mode = 3;
        g_linear_framebuffer = (volatile unsigned char*)0;
        gfx_clear(0);
        return -2;
    }
    if (mode == 2) {
        (void)vbe_set_display_mode(2);
        write_vga_registers(g_mode13_regs);
        g_current_mode = 2;
        set_rgb332_palette();
        gfx_clear(0);
        return 0;
    }
    if (mode == 3) {
        (void)vbe_set_display_mode(1);
        write_vga_registers(g_mode12_regs);
        g_current_mode = 3;
        g_linear_framebuffer = (volatile unsigned char*)0;
        gfx_clear(0);
        return 0;
    }
    if (mode == 0) {
        (void)vbe_set_display_mode(0);
        write_vga_registers(g_mode3_regs);
        g_current_mode = 0;
        g_linear_framebuffer = (volatile unsigned char*)0;
        clear_screen();
        enable_hardware_cursor();
        return 0;
    }
    return -1;
}

static int gfx_get_mode(void) { return g_current_mode; }

static void gfx_put_pixel(int x, int y, unsigned char color) {
    if (g_current_mode == 1) {
        if (x >= 0 && (unsigned int)x < g_linear_width &&
            y >= 0 && (unsigned int)y < g_linear_height)
            g_linear_framebuffer[(unsigned int)y * g_linear_pitch + (unsigned int)x] = color;
    } else if (g_current_mode == 3) {
        if (x >= 0 && x < MODE_640_WIDTH && y >= 0 && y < MODE_640_HEIGHT)
            planar_put_pixel(x, y, color);
    } else if (g_current_mode == 2 && x >= 0 && x < MODE_320_WIDTH && y >= 0 && y < MODE_320_HEIGHT) {
        VGA_GFX_ADDR[y * MODE_320_WIDTH + x] = color;
    }
}

static void gfx_fill_rect(int x, int y, int w, int h, unsigned char color) {
    if (g_current_mode == 1) {
        if (x < 0) { w += x; x = 0; }
        if (y < 0) { h += y; y = 0; }
        if (x + w > (int)g_linear_width) w = (int)g_linear_width - x;
        if (y + h > (int)g_linear_height) h = (int)g_linear_height - y;
        if (w > 0 && h > 0)
            for (int row = 0; row < h; row++)
                memset((void*)&g_linear_framebuffer[(y + row) * g_linear_pitch + x], color, (unsigned int)w);
        return;
    }
    if (g_current_mode == 3) {
        planar_fill_rect(x, y, w, h, color);
        return;
    }
    if (g_current_mode != 2) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > MODE_320_WIDTH) w = MODE_320_WIDTH - x;
    if (y + h > MODE_320_HEIGHT) h = MODE_320_HEIGHT - y;
    if (w <= 0 || h <= 0) return;
    for (int row = 0; row < h; row++)
        memset((void*)&VGA_GFX_ADDR[(y + row) * MODE_320_WIDTH + x], color, (unsigned int)w);
}

static unsigned char* gfx_get_framebuffer(void) {
    if (g_current_mode == 1) return (unsigned char*)g_linear_framebuffer;
    return g_current_mode == 2 ? (unsigned char*)VGA_GFX_ADDR : (unsigned char*)0;
}

static int gfx_get_info(cyk_display_info_t* info) {
    if (!info) return -1;
    if (g_current_mode == 1) {
        info->width = g_linear_width;
        info->height = g_linear_height;
        info->pitch = g_linear_pitch;
        info->bits_per_pixel = 8;
        info->pixel_format = CYK_PIXEL_INDEX8;
        info->framebuffer = (unsigned char*)g_linear_framebuffer;
    } else if (g_current_mode == 2) {
        info->width = MODE_320_WIDTH;
        info->height = MODE_320_HEIGHT;
        info->pitch = MODE_320_WIDTH;
        info->bits_per_pixel = 8;
        info->pixel_format = CYK_PIXEL_INDEX8;
        info->framebuffer = (unsigned char*)VGA_GFX_ADDR;
    } else if (g_current_mode == 3) {
        info->width = MODE_640_WIDTH;
        info->height = MODE_640_HEIGHT;
        info->pitch = MODE_640_PITCH;
        info->bits_per_pixel = 4;
        info->pixel_format = CYK_PIXEL_VGA_PLANAR4;
        info->framebuffer = (unsigned char*)0;
    } else {
        info->width = 80;
        info->height = 25;
        info->pitch = 160;
        info->bits_per_pixel = 16;
        info->pixel_format = CYK_PIXEL_TEXT16;
        info->framebuffer = (unsigned char*)0xB8000;
    }
    return 0;
}

static int gfx_init(void) {
    g_current_mode = 0;
    g_cached_set_reset = -1;
    g_linear_framebuffer = (volatile unsigned char*)0;
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
    .get_info = gfx_get_info,
    .draw_bitmap_ex = gfx_draw_bitmap_ex,
    .set_palette = gfx_set_palette,
};
