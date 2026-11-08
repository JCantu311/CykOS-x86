#![no_std]
#![no_main]

use core::panic::PanicInfo;

#[no_mangle]
pub extern "C" fn kernel_main() -> ! {
    // You are now running 32-bit execution context inside Rust!
    // You can interact with 32-bit memory-mapped I/O here.
    
    let vga_buffer = 0xb8000 as *mut u16;
    unsafe {
        // Write a bright white 'H' on a black background to the screen
        *vga_buffer = 0x0F48; 
    }

    loop {}
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {}
}
