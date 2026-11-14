//! Graphics Driver & Framebuffer Demo (Mode 13h: 320x200 256-color)
//! Demonstrates switching modes, bitmap rendering, geometry, color palette, and animation.

#![no_std]
#![no_main]

use core::panic::PanicInfo;

mod cykos;
use cykos::*;

#[no_mangle]
#[link_section = ".entry"]
pub extern "C" fn main(_argc: isize, _argv: *const *const u8) -> i32 {
    // 1. Switch to 320x200 256-color graphics mode
    if gfx_set_mode(1) != 0 {
        println("Error: Failed to initialize graphics mode (Display driver not loaded).");
        return -1;
    }

    // Clear background to dark navy blue (color 1)
    gfx_clear(1);

    // 2. Draw colored title header bar
    gfx_fill_rect(0, 0, 320, 20, 9); // Light blue bar
    gfx_fill_rect(0, 180, 320, 20, 8); // Dark grey status bar

    // 3. Draw 256-color palette gradient bars
    let bar_y: i16 = 30;
    for col in 0..256 {
        let x = 32 + (col as i16);
        gfx_fill_rect(x, bar_y, 1, 16, col as u8);
    }

    // 4. Draw geometric shapes (nested rectangles)
    let colors: [u8; 6] = [10, 14, 12, 13, 11, 15];
    for (i, &col) in colors.iter().enumerate() {
        let step = (i as i16) * 6;
        gfx_fill_rect(30 + step, 60 + step, 100 - step * 2, 80 - step * 2, col);
    }

    // 5. Draw direct bitmap image into framebuffer
    // Let's draw an 8x8 smiley bitmap scaled up to 32x32
    #[rustfmt::skip]
    let smiley: [u8; 64] = [
        0, 0, 1, 1, 1, 1, 0, 0,
        0, 1, 14, 14, 14, 14, 1, 0,
        1, 14, 0, 14, 14, 0, 14, 1,
        1, 14, 14, 14, 14, 14, 14, 1,
        1, 14, 0, 14, 14, 0, 14, 1,
        1, 14, 0, 0, 0, 0, 14, 1,
        0, 1, 14, 14, 14, 14, 1, 0,
        0, 0, 1, 1, 1, 1, 0, 0,
    ];

    let bmp_ox: i16 = 180;
    let bmp_oy: i16 = 70;
    for r in 0..8 {
        for c in 0..8 {
            let color = smiley[r * 8 + c];
            if color != 0 {
                gfx_fill_rect(bmp_ox + (c as i16) * 5, bmp_oy + (r as i16) * 5, 5, 5, color);
            }
        }
    }

    // 6. Direct framebuffer test: plasma animation
    // User can exit at ANY time by pressing ESC (0x1B), 'q', Space, or Ctrl+C (0x03)
    let fb = gfx_framebuffer();
    let mut exit_requested = false;

    if !fb.is_null() {
        for frame in 0..100 {
            if key_available() {
                let k = read_char();
                if k == 0x1B || k == b'q' || k == b'Q' || k == 0x03 || k == b' ' {
                    exit_requested = true;
                    break;
                }
            }

            // Draw a bouncing color wave inside a box
            let bx: usize = 240;
            let by: usize = 60;
            let bw: usize = 60;
            let bh: usize = 60;

            for dy in 0..bh {
                for dx in 0..bw {
                    let offset = (by + dy) * 320 + (bx + dx);
                    let color = ((dx + dy + frame * 2) & 0xFF) as u8;
                    unsafe {
                        *fb.add(offset) = color;
                    }
                }
            }

            msleep(20);
        }
    }

    // If user didn't already exit during animation, chime and wait for any key / ESC / Q / Space
    if !exit_requested {
        sound_chime();

        loop {
            if key_available() {
                let _k = read_char();
                break;
            }
            msleep(25);
        }
    }

    // 7. Restore 80x25 text mode before exiting to shell
    gfx_set_mode(0);
    0
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {
        unsafe { core::arch::asm!("hlt"); }
    }
}
