#![no_std]
#![no_main]

use core::panic::PanicInfo;

extern "C" {
    fn print_string(s: *const u8);
    fn vfs_read_file_by_name(filename: *const u8, buffer: *mut u8, max_bytes: u32) -> i32;
    fn vfs_write_file(filename: *const u8, data: *const u8, size: u32) -> i32;
}

#[no_mangle]
#[link_section = ".entry"]
pub extern "C" fn main(argc: isize, argv: *const *const u8) -> i32 {
    if argc < 2 || argv.is_null() {
        unsafe { print_string(b"Usage: touch <filename>\n\0".as_ptr()); }
        return 1;
    }

    unsafe {
        let filename = *argv.add(1);
        if filename.is_null() {
            print_string(b"Error: missing filename\n\0".as_ptr());
            return 1;
        }

        // Check if file already exists
        let mut check_buf = [0u8; 1];
        let exists = vfs_read_file_by_name(filename, check_buf.as_mut_ptr(), 1);

        if exists < 0 {
            // File does not exist, create empty file (size 0)
            let dummy = 0u8;
            let res = vfs_write_file(filename, &dummy, 0);
            if res == 0 {
                print_string(b"Created new file: \0".as_ptr());
                print_string(filename);
                print_string(b"\n\0".as_ptr());
            } else {
                print_string(b"touch: failed to create file\n\0".as_ptr());
                return 1;
            }
        } else {
            // File exists: touched successfully
            print_string(b"Touched existing file: \0".as_ptr());
            print_string(filename);
            print_string(b"\n\0".as_ptr());
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
