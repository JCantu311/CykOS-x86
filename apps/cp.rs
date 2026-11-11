#![no_std]
#![no_main]

use core::panic::PanicInfo;

extern "C" {
    fn print_string(s: *const u8);
    fn vfs_read_file_by_name(filename: *const u8, buffer: *mut u8, max_bytes: u32) -> i32;
    fn vfs_write_file(filename: *const u8, data: *const u8, size: u32) -> i32;
}

static mut FILE_BUF: [u8; 65536] = [0; 65536];

#[no_mangle]
#[link_section = ".entry"]
pub extern "C" fn main(argc: isize, argv: *const *const u8) -> i32 {
    if argc < 3 || argv.is_null() {
        unsafe { print_string(b"Usage: cp <source> <destination>\n\0".as_ptr()); }
        return 1;
    }

    unsafe {
        let src = *argv.add(1);
        let dst = *argv.add(2);
        if src.is_null() || dst.is_null() {
            print_string(b"Error: invalid arguments\n\0".as_ptr());
            return 1;
        }

        let bytes = vfs_read_file_by_name(src, FILE_BUF.as_mut_ptr(), 65536);
        if bytes < 0 {
            print_string(b"cp: cannot open source file: \0".as_ptr());
            print_string(src);
            print_string(b"\n\0".as_ptr());
            return 1;
        }

        let res = vfs_write_file(dst, FILE_BUF.as_ptr(), bytes as u32);
        if res == 0 {
            print_string(b"Copied \0".as_ptr());
            print_string(src);
            print_string(b" -> \0".as_ptr());
            print_string(dst);
            print_string(b"\n\0".as_ptr());
        } else {
            print_string(b"cp: failed to write destination file: \0".as_ptr());
            print_string(dst);
            print_string(b"\n\0".as_ptr());
            return 1;
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
