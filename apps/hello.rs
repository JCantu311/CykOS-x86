#![no_std]
#![no_main]

use core::panic::PanicInfo;
#[path = "../src/user/syscall.rs"]
mod syscall;
use syscall::{print_char, print_string};

#[no_mangle]
#[link_section = ".entry"]
pub extern "C" fn main(argc: isize, argv: *const *const u8) -> i32 {
    unsafe {
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
    unsafe { syscall::exit(101) }
}
