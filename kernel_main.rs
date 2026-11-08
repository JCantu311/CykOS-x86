#![no_std]
#![no_main]

use core::panic::PanicInfo;

#[unsafe(no_mangle)] // Updated syntax for Rust Edition 2024
pub extern "C" fn kernel_main() -> ! {
    let vga_buffer = 0xb8000 as *mut u16;

    const SCREEN_SIZE: usize = 80 * 25; // 80 columns * 25 rows

    const BLANK_SPACE: u16 = 0x0F20; // Bright White space character

    unsafe {
        for i in 0..SCREEN_SIZE {
            *vga_buffer.offset(i as isize) = BLANK_SPACE;
        }

        *vga_buffer.offset(0) = 0x0F4F; // Bright White 'O'
        *vga_buffer.offset(1) = 0x0F4B; // Bright White 'K'
    }

    loop {}
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {}
}
