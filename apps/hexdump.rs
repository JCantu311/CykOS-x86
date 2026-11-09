#![no_std]
#![no_main]

use core::panic::PanicInfo;

extern "C" {
    fn print_string(s: *const u8);
    fn print_char(c: u8);
    fn fat16_read_file_by_name(filename: *const u8, buffer: *mut u8, max_bytes: u32) -> i32;
}

static mut FILE_BUF: [u8; 16384] = [0; 16384];

const HEX_CHARS: &[u8; 16] = b"0123456789ABCDEF";

fn print_hex_byte(b: u8) {
    let hi = ((b >> 4) & 0x0F) as usize;
    let lo = (b & 0x0F) as usize;
    unsafe {
        print_char(HEX_CHARS[hi]);
        print_char(HEX_CHARS[lo]);
    }
}

fn print_hex_u32(val: u32) {
    print_hex_byte(((val >> 24) & 0xFF) as u8);
    print_hex_byte(((val >> 16) & 0xFF) as u8);
    print_hex_byte(((val >> 8) & 0xFF) as u8);
    print_hex_byte((val & 0xFF) as u8);
}

#[no_mangle]
#[link_section = ".entry"]
pub extern "C" fn main(argc: isize, argv: *const *const u8) -> i32 {
    if argc < 2 || argv.is_null() {
        unsafe { print_string(b"Usage: hexdump <filename>\n\0".as_ptr()); }
        return 1;
    }

    unsafe {
        let filename = *argv.add(1);
        if filename.is_null() {
            print_string(b"Error: missing filename\n\0".as_ptr());
            return 1;
        }

        let bytes_read = fat16_read_file_by_name(filename, FILE_BUF.as_mut_ptr(), 16384);
        if bytes_read < 0 {
            print_string(b"hexdump: cannot open file: \0".as_ptr());
            print_string(filename);
            print_string(b"\n\0".as_ptr());
            return 1;
        }

        let total = bytes_read as usize;
        let mut offset = 0;

        while offset < total {
            // Print offset: 00000000:
            print_hex_u32(offset as u32);
            print_string(b": \0".as_ptr());

            // Print hex bytes (16 per line)
            let mut i = 0;
            while i < 16 {
                if offset + i < total {
                    let b = *FILE_BUF.as_ptr().add(offset + i);
                    print_hex_byte(b);
                    print_char(b' ');
                } else {
                    print_string(b"   \0".as_ptr());
                }
                if i == 7 {
                    print_char(b' ');
                }
                i += 1;
            }

            // Print ASCII representation: |...|
            print_string(b" |\0".as_ptr());
            i = 0;
            while i < 16 {
                if offset + i < total {
                    let b = *FILE_BUF.as_ptr().add(offset + i);
                    if b >= 32 && b <= 126 {
                        print_char(b);
                    } else {
                        print_char(b'.');
                    }
                }
                i += 1;
            }
            print_string(b"|\n\0".as_ptr());

            offset += 16;
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
