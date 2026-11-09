//! CykOS Userland Application SDK (Rust)
//! Freestanding, 100% #![no_std].

#![no_std]

// Raw Kernel Declarations
pub mod sys {
    extern "C" {
        pub fn clear_screen();
        pub fn print_char(c: u8);
        pub fn print_string(s: *const u8);
        pub fn print_color_string(s: *const u8, color: u8);
        pub fn update_hardware_cursor();
        pub fn enable_hardware_cursor();

        pub fn inb(port: u16) -> u8;
        pub fn outb(port: u16, data: u8);
        pub fn inw(port: u16) -> u16;
        pub fn outw(port: u16, data: u16);

        pub fn get_char() -> u8;
        pub fn has_key() -> i32;

        pub fn fat16_read_file_by_name(filename: *const u8, buffer: *mut u8, max_bytes: u32) -> i32;
        pub fn fat16_write_file(filename: *const u8, data: *const u8, size: u32) -> i32;
    }
}

// Colors
pub const COLOR_BLACK: u8 = 0x00;
pub const COLOR_BLUE: u8 = 0x01;
pub const COLOR_GREEN: u8 = 0x02;
pub const COLOR_CYAN: u8 = 0x03;
pub const COLOR_RED: u8 = 0x04;
pub const COLOR_MAGENTA: u8 = 0x05;
pub const COLOR_BROWN: u8 = 0x06;
pub const COLOR_LIGHT_GREY: u8 = 0x07;
pub const COLOR_DARK_GREY: u8 = 0x08;
pub const COLOR_LIGHT_BLUE: u8 = 0x09;
pub const COLOR_LIGHT_GREEN: u8 = 0x0A;
pub const COLOR_LIGHT_CYAN: u8 = 0x0B;
pub const COLOR_LIGHT_RED: u8 = 0x0C;
pub const COLOR_LIGHT_MAGENTA: u8 = 0x0D;
pub const COLOR_YELLOW: u8 = 0x0E;
pub const COLOR_WHITE: u8 = 0x0F;

// Safe, ergonomic wrappers
pub fn clear() {
    unsafe { sys::clear_screen(); }
}

pub fn putc(c: u8) {
    unsafe { sys::print_char(c); }
}

pub fn print(s: &str) {
    for b in s.bytes() {
        unsafe { sys::print_char(b); }
    }
}

pub fn println(s: &str) {
    print(s);
    putc(b'\n');
}

pub fn print_color(s: &str, color: u8) {
    for b in s.bytes() {
        let buf = [b, 0];
        unsafe { sys::print_color_string(buf.as_ptr(), color); }
    }
}

pub fn read_char() -> u8 {
    unsafe { sys::get_char() }
}

pub fn key_available() -> bool {
    unsafe { sys::has_key() != 0 }
}

pub fn set_cursor(col: u8, row: u8) {
    let pos = (row as u16) * 80 + (col as u16);
    unsafe {
        sys::outb(0x3D4, 0x0E);
        sys::outb(0x3D5, (pos >> 8) as u8);
        sys::outb(0x3D4, 0x0F);
        sys::outb(0x3D5, (pos & 0xFF) as u8);
    }
}

pub fn read_file(filename: &str, buf: &mut [u8]) -> Result<usize, i32> {
    let mut fname_buf = [0u8; 16];
    let len = if filename.len() < 15 { filename.len() } else { 15 };
    fname_buf[..len].copy_from_slice(&filename.as_bytes()[..len]);
    fname_buf[len] = 0;

    let res = unsafe {
        sys::fat16_read_file_by_name(fname_buf.as_ptr(), buf.as_mut_ptr(), buf.len() as u32)
    };
    if res >= 0 {
        Ok(res as usize)
    } else {
        Err(res)
    }
}

pub fn write_file(filename: &str, data: &[u8]) -> Result<(), i32> {
    let mut fname_buf = [0u8; 16];
    let len = if filename.len() < 15 { filename.len() } else { 15 };
    fname_buf[..len].copy_from_slice(&filename.as_bytes()[..len]);
    fname_buf[len] = 0;

    let res = unsafe {
        sys::fat16_write_file(fname_buf.as_ptr(), data.as_ptr(), data.len() as u32)
    };
    if res == 0 {
        Ok(())
    } else {
        Err(res)
    }
}

/// Standard command-line argument reader
pub struct Args {
    argc: isize,
    argv: *const *const u8,
}

impl Args {
    /// Create Args from standard (argc, argv) entry point parameters
    pub unsafe fn from_raw(argc: isize, argv: *const *const u8) -> Self {
        Self { argc, argv }
    }

    /// Number of arguments (including argv[0] program name)
    pub fn len(&self) -> usize {
        if self.argc > 0 { self.argc as usize } else { 0 }
    }

    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// Get argument at index as string slice
    pub fn get(&self, index: usize) -> Option<&'static str> {
        if index >= self.len() || self.argv.is_null() {
            return None;
        }
        unsafe {
            let ptr = *self.argv.add(index);
            if ptr.is_null() {
                return None;
            }
            let mut len = 0;
            while *ptr.add(len) != 0 {
                len += 1;
            }
            let slice = core::slice::from_raw_parts(ptr, len);
            core::str::from_utf8(slice).ok()
        }
    }
}
