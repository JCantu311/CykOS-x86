#![no_std]
#![no_main]

use core::panic::PanicInfo;

#[no_mangle]
pub extern "C" fn kernel_main() -> ! {
    // Print "OK" directly to the top-left of the VGA Text Mode screen
    let vga_buffer = 0xb8000 as *mut u16;
    unsafe {
        *vga_buffer.offset(0) = 0x0F4F; // Bright White 'O'
        *vga_buffer.offset(1) = 0x0F4B; // Bright White 'K'
    }

    loop {}
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {}
}
