#![no_std]
#![no_main]

use core::panic::PanicInfo;
use core::ptr::write_volatile;

#[unsafe(no_mangle)]
pub extern "C" fn kernel_main() -> ! {
    let vga_buffer = 0xb8000 as *mut u16;

    const SCREEN_SIZE: usize = 80 * 25;
    const BLANK_SPACE: u16 = 0x0F20; // Space character, white on black

    unsafe {
        // 1. Clear the screen using strict volatile writes
        for i in 0..SCREEN_SIZE {
            write_volatile(vga_buffer.add(i), BLANK_SPACE);
        }

        // 2. Print "OK" using volatile writes
        write_volatile(vga_buffer.add(0), 0x0F4F); // Bright White 'O'
        write_volatile(vga_buffer.add(1), 0x0F4B); // Bright White 'K'
    }

    loop {}
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {}
}
