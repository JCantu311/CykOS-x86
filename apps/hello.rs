#![no_std]
#![no_main]

use core::panic::PanicInfo;

extern "C" {
    fn print_string(s: *const u8);
    fn print_char(c: u8);
    fn get_char() -> u8;
}

#[no_mangle]
#[link_section = ".entry"]
pub extern "C" fn main(argc: isize, argv: *const *const u8) -> i32 {
    unsafe {
        // Direct VGA canary: write "HELLO" in Yellow (0x0E) at row 0, col 70
        // If this appears, we KNOW execution successfully reached main() at 0x50000!
        let vga = 0xB8000 as *mut u16;
        *vga.add(70) = 0x0E48; // 'H'
        *vga.add(71) = 0x0E45; // 'E'
        *vga.add(72) = 0x0E4C; // 'L'
        *vga.add(73) = 0x0E4C; // 'L'
        *vga.add(74) = 0x0E4F; // 'O'

        print_string(b"========================================\n\0".as_ptr());
        print_string(b"  Hello from Rust Userland App!\n\0".as_ptr());
        print_string(b"========================================\n\0".as_ptr());

        let count = if argc > 0 { argc as usize } else { 0 };
        print_string(b"Argument count (argc): \0".as_ptr());
        print_char(b'0' + (count as u8));
        print_string(b"\n\0".as_ptr());

        for i in 0..count {
            print_string(b"  argv[\0".as_ptr());
            print_char(b'0' + (i as u8));
            print_string(b"] = '\0".as_ptr());
            let arg_ptr = *argv.add(i);
            if !arg_ptr.is_null() {
                print_string(arg_ptr);
            }
            print_string(b"'\n\0".as_ptr());
        }
    }
    0
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {
        unsafe { core::arch::asm!("hlt"); }
    }
}
