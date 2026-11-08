#![no_std]
#![no_main]

use core::panic::PanicInfo;

#[unsafe(no_mangle)]
pub extern "C" fn kernel_main() -> ! {
    let vga_buffer = 0xb8000 as *mut u16;

    // VGA text mode screens are exactly 80 columns by 25 rows
    const SCREEN_SIZE: usize = 80 * 25;
    
    // A blank space (0x20) with a black background / white foreground (0x0F)
    const BLANK_SPACE: u16 = 0x0F20;

    unsafe {
        // 1. Clear the screen by writing blank spaces to the entire buffer
        for i in 0..SCREEN_SIZE {
            *vga_buffer.add(i) = BLANK_SPACE;
        }

        // 2. Print "OK" at the very top left (offsets 0 and 1)
        *vga_buffer.add(0) = 0x0F4F; // Bright White 'O'
        *vga_buffer.add(1) = 0x0F4B; // Bright White 'K'
    }

    loop {}
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {}
}
