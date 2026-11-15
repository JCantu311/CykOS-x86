//! 640x480 indexed graphics and standardized bitmap API demonstration.

#![no_std]
#![no_main]

use core::panic::PanicInfo;

mod cykos;
use cykos::*;

#[no_mangle]
#[link_section = ".entry"]
pub extern "C" fn main(_argc: isize, _argv: *const *const u8) -> i32 {
    if gfx_set_mode(GFX_MODE_640X480) != 0 {
        gfx_set_mode(GFX_MODE_TEXT);
        println("Error: 640x480 graphics mode unavailable.");
        return -1;
    }

    let mut info = GfxInfo {
        width: 0,
        height: 0,
        pitch: 0,
        bits_per_pixel: 0,
        pixel_format: 0,
        framebuffer: core::ptr::null_mut(),
    };
    if gfx_get_info(&mut info) != 0 || info.width != 640 || info.height != 480 {
        gfx_set_mode(GFX_MODE_TEXT);
        println("Error: display mode information is invalid.");
        return -1;
    }

    gfx_clear(1);
    gfx_fill_rect(0, 0, 640, 24, 9);
    gfx_fill_rect(0, 456, 640, 24, 8);

    // Demonstrate a transparent RGBA bitmap with a 48-byte row pitch.
    const SMILEY: [u8; 64] = [
        0,0,1,1,1,1,0,0,
        0,1,14,14,14,14,1,0,
        1,14,0,14,14,0,14,1,
        1,14,14,14,14,14,14,1,
        1,14,0,14,14,0,14,1,
        1,14,0,0,0,0,14,1,
        0,1,14,14,14,14,1,0,
        0,0,1,1,1,1,0,0,
    ];
    let mut scaled_rgba = [0u8; 48 * 48 * 4];
    for y in 0..48usize {
        for x in 0..48usize {
            let source = SMILEY[(y / 6) * 8 + (x / 6)];
            let dst = (y * 48 + x) * 4;
            let rgba = match source {
                0 => [0, 0, 0, 0],
                1 => [0, 0, 255, 255],
                14 => [255, 255, 0, 255],
                _ => [255, 255, 255, 255],
            };
            scaled_rgba[dst..dst + 4].copy_from_slice(&rgba);
        }
    }
    let _ = gfx_draw_bitmap(510, 48, 48, 48, 48 * 4, GFX_BITMAP_RGBA32, &scaled_rgba);

    // RGB332 palette samples exercise its full range of 256 indexed colors.
    let colors = [0, 3, 7, 28, 31, 35, 63, 96, 128, 160, 192, 224, 227, 236, 252, 255];
    for (slot, &color) in colors.iter().enumerate() {
        gfx_fill_rect(32 + slot as i16 * 28, 60, 24, 18, color);
    }
    gfx_fill_rect(32, 100, 240, 3, 255);
    gfx_fill_rect(32, 103, 3, 180, 255);
    gfx_fill_rect(32, 280, 240, 3, 255);
    gfx_fill_rect(269, 103, 3, 180, 255);

    // Moving bitmap-like sprite, drawn with the same generic rectangle path.
    let mut previous_x = 0i16;
    for frame in 0..90i16 {
        if let Some(key) = try_read_char() {
            if key == 0x1B || key == b'q' || key == b'Q' || key == 0x03 { break; }
        }
        let x = 40 + frame * 6;
        if frame > 0 { gfx_fill_rect(previous_x, 330, 30, 30, 1); }
        gfx_fill_rect(x, 330, 30, 30, (frame as u8 % 15) + 1);
        previous_x = x;
        msleep(25);
    }

    gfx_set_mode(GFX_MODE_TEXT);
    0
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop { unsafe { core::arch::asm!("hlt"); } }
}
