#![no_std]
#![no_main]

use core::panic::PanicInfo;

extern "C" {
    fn print_string(s: *const u8);
    fn print_char(c: u8);
    fn fat16_read_file_by_name(filename: *const u8, buffer: *mut u8, max_bytes: u32) -> i32;
}

static mut FILE_BUF: [u8; 65536] = [0; 65536];

fn print_u32(mut n: u32) {
    if n == 0 {
        unsafe { print_char(b'0'); }
        return;
    }
    let mut buf = [0u8; 10];
    let mut idx = 0;
    while n > 0 {
        buf[idx] = b'0' + (n % 10) as u8;
        n /= 10;
        idx += 1;
    }
    while idx > 0 {
        idx -= 1;
        unsafe { print_char(buf[idx]); }
    }
}

#[no_mangle]
#[link_section = ".entry"]
pub extern "C" fn main(argc: isize, argv: *const *const u8) -> i32 {
    if argc < 2 || argv.is_null() {
        unsafe { print_string(b"Usage: wc <filename>\n\0".as_ptr()); }
        return 1;
    }

    unsafe {
        let filename = *argv.add(1);
        if filename.is_null() {
            print_string(b"Error: missing filename\n\0".as_ptr());
            return 1;
        }

        let bytes_read = fat16_read_file_by_name(filename, FILE_BUF.as_mut_ptr(), 65536);
        if bytes_read < 0 {
            print_string(b"wc: cannot open file: \0".as_ptr());
            print_string(filename);
            print_string(b"\n\0".as_ptr());
            return 1;
        }

        let total_bytes = bytes_read as usize;
        let mut lines: u32 = 0;
        let mut words: u32 = 0;
        let mut in_word = false;

        let mut i = 0;
        while i < total_bytes {
            let b = *FILE_BUF.as_ptr().add(i);
            if b == b'\n' {
                lines += 1;
            }
            if b == b' ' || b == b'\t' || b == b'\n' || b == b'\r' {
                in_word = false;
            } else if !in_word {
                in_word = true;
                words += 1;
            }
            i += 1;
        }

        // Output formatting: lines  words  bytes  filename
        print_string(b"  \0".as_ptr());
        print_u32(lines);
        print_string(b"  \0".as_ptr());
        print_u32(words);
        print_string(b"  \0".as_ptr());
        print_u32(total_bytes as u32);
        print_string(b"  \0".as_ptr());
        print_string(filename);
        print_string(b"\n\0".as_ptr());
    }

    0
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {
        unsafe { core::arch::asm!("hlt"); }
    }
}
