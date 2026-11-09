//! CykOS Userland Application SDK (Rust)
//! 100% #![no_std] using the int 0x80 System Call Interface.

#![no_std]

// =========================================================================
// CykOS System Call Numbers
// =========================================================================
pub const SYS_EXIT: u32       = 1;
pub const SYS_READ: u32       = 3;
pub const SYS_WRITE: u32      = 4;
pub const SYS_OPEN: u32       = 5;
pub const SYS_CLOSE: u32      = 6;
pub const SYS_BRK: u32        = 45;
pub const SYS_CLEAR: u32      = 100;
pub const SYS_SET_CURSOR: u32 = 101;
pub const SYS_GET_KEY: u32    = 102;
pub const SYS_HAS_KEY: u32    = 103;
pub const SYS_SLEEP: u32      = 104;
pub const SYS_FAT_READ: u32   = 105;
pub const SYS_FAT_WRITE: u32  = 106;

pub const FD_STDIN: u32  = 0;
pub const FD_STDOUT: u32 = 1;
pub const FD_STDERR: u32 = 2;

// =========================================================================
// Low-Level int 0x80 Syscall Dispatchers
// =========================================================================
pub mod syscall {
    #[inline(always)]
    pub unsafe fn syscall0(num: u32) -> u32 {
        let ret: u32;
        core::arch::asm!(
            "int 0x80",
            inout("eax") num => ret,
            options(nostack)
        );
        ret
    }

    #[inline(always)]
    pub unsafe fn syscall1(num: u32, arg1: u32) -> u32 {
        let ret: u32;
        core::arch::asm!(
            "int 0x80",
            inout("eax") num => ret,
            in("ebx") arg1,
            options(nostack)
        );
        ret
    }

    #[inline(always)]
    pub unsafe fn syscall2(num: u32, arg1: u32, arg2: u32) -> u32 {
        let ret: u32;
        core::arch::asm!(
            "int 0x80",
            inout("eax") num => ret,
            in("ebx") arg1,
            in("ecx") arg2,
            options(nostack)
        );
        ret
    }

    #[inline(always)]
    pub unsafe fn syscall3(num: u32, arg1: u32, arg2: u32, arg3: u32) -> u32 {
        let ret: u32;
        core::arch::asm!(
            "int 0x80",
            inout("eax") num => ret,
            in("ebx") arg1,
            in("ecx") arg2,
            in("edx") arg3,
            options(nostack)
        );
        ret
    }
}

// =========================================================================
// Safe Userland API Wrappers
// =========================================================================

/// Clear the screen
pub fn clear() {
    unsafe { syscall::syscall0(SYS_CLEAR); }
}

/// Print a single character to standard output
pub fn putc(c: u8) {
    unsafe { syscall::syscall3(SYS_WRITE, FD_STDOUT, (&c as *const u8) as u32, 1); }
}

/// Print a byte slice to standard output
pub fn print_bytes(bytes: &[u8]) {
    if !bytes.is_empty() {
        unsafe {
            syscall::syscall3(SYS_WRITE, FD_STDOUT, bytes.as_ptr() as u32, bytes.len() as u32);
        }
    }
}

/// Print a string slice to standard output
pub fn print(s: &str) {
    print_bytes(s.as_bytes());
}

/// Print a string slice followed by a newline
pub fn println(s: &str) {
    print(s);
    putc(b'\n');
}

/// Print an error message to standard error (rendered in Red)
pub fn print_err(s: &str) {
    if !s.is_empty() {
        unsafe {
            syscall::syscall3(SYS_WRITE, FD_STDERR, s.as_bytes().as_ptr() as u32, s.len() as u32);
        }
    }
}

/// Position the hardware text cursor
pub fn set_cursor(col: u8, row: u8) {
    unsafe { syscall::syscall2(SYS_SET_CURSOR, col as u32, row as u32); }
}

/// Read a single character from the keyboard (blocking)
pub fn read_char() -> u8 {
    unsafe { syscall::syscall0(SYS_GET_KEY) as u8 }
}

/// Check if a keystroke is pending
pub fn key_available() -> bool {
    unsafe { syscall::syscall0(SYS_HAS_KEY) != 0 }
}

/// Sleep for specified seconds
pub fn sleep(seconds: u32) {
    unsafe { syscall::syscall1(SYS_SLEEP, seconds); }
}

/// Read a file from FAT16 by name into a buffer
pub fn read_file(filename: &str, buf: &mut [u8]) -> Result<usize, i32> {
    let mut fname_buf = [0u8; 16];
    let len = if filename.len() < 15 { filename.len() } else { 15 };
    for i in 0..len {
        fname_buf[i] = filename.as_bytes()[i];
    }
    fname_buf[len] = 0;

    let res = unsafe {
        syscall::syscall3(SYS_FAT_READ, fname_buf.as_ptr() as u32, buf.as_mut_ptr() as u32, buf.len() as u32) as i32
    };

    if res >= 0 {
        Ok(res as usize)
    } else {
        Err(res)
    }
}

/// Write a file to FAT16 by name
pub fn write_file(filename: &str, data: &[u8]) -> Result<(), i32> {
    let mut fname_buf = [0u8; 16];
    let len = if filename.len() < 15 { filename.len() } else { 15 };
    for i in 0..len {
        fname_buf[i] = filename.as_bytes()[i];
    }
    fname_buf[len] = 0;

    let res = unsafe {
        syscall::syscall3(SYS_FAT_WRITE, fname_buf.as_ptr() as u32, data.as_ptr() as u32, data.len() as u32) as i32
    };

    if res == 0 {
        Ok(())
    } else {
        Err(res)
    }
}

/// Exit the current program and return to the shell
pub fn exit(code: i32) -> ! {
    unsafe {
        syscall::syscall1(SYS_EXIT, code as u32);
        loop { core::arch::asm!("hlt"); }
    }
}

// =========================================================================
// Command-line Argument Parser
// =========================================================================
pub struct Args {
    argc: isize,
    argv: *const *const u8,
}

impl Args {
    pub unsafe fn from_raw(argc: isize, argv: *const *const u8) -> Self {
        Self { argc, argv }
    }

    pub fn len(&self) -> usize {
        if self.argc > 0 { self.argc as usize } else { 0 }
    }

    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

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
