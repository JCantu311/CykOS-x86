//! Minimal application ABI. These stubs are linked into the application so
//! user code never branches directly into privileged kernel text.

#![allow(dead_code)]

const SYS_GET_KEY: u32 = 102;
const SYS_HAS_KEY: u32 = 103;
const SYS_FAT_READ: u32 = 105;
const SYS_FAT_WRITE: u32 = 106;
const SYS_CLEAR_SCREEN: u32 = 100;
const SYS_PRINT_CHAR: u32 = 202;
const SYS_PRINT_STRING: u32 = 203;
const SYS_SET_CURSOR: u32 = 101;
const SYS_TEXT_WRITE_CELL: u32 = 207;
const SYS_EXIT: u32 = 1;

#[inline(always)]
unsafe fn syscall3(number: u32, a1: u32, a2: u32, a3: u32) -> i32 {
    let result: i32;
    core::arch::asm!(
        "int 0x80",
        inlateout("eax") number as i32 => result,
        in("ebx") a1,
        in("ecx") a2,
        in("edx") a3,
        options()
    );
    result
}

#[inline(always)]
unsafe fn syscall1(number: u32, a1: u32) -> i32 {
    syscall3(number, a1, 0, 0)
}

pub unsafe fn clear_screen() {
    let _ = syscall1(SYS_CLEAR_SCREEN, 0);
}

pub unsafe fn print_char(value: u8) {
    let _ = syscall1(SYS_PRINT_CHAR, value as u32);
}

pub unsafe fn print_string(value: *const u8) {
    let _ = syscall1(SYS_PRINT_STRING, value as u32);
}

pub unsafe fn get_char() -> u8 {
    syscall1(SYS_GET_KEY, 0) as u8
}

pub unsafe fn has_key() -> i32 {
    syscall1(SYS_HAS_KEY, 0)
}

pub unsafe fn set_cursor(col: u32, row: u32) -> i32 {
    syscall3(SYS_SET_CURSOR, col, row, 0)
}

pub unsafe fn write_text_cell(col: u32, row: u32, character: u8, attribute: u8) -> i32 {
    let position = (row << 16) | col;
    let cell = ((attribute as u32) << 8) | character as u32;
    syscall3(SYS_TEXT_WRITE_CELL, position, cell, 0)
}

pub unsafe fn exit(code: i32) -> ! {
    let _ = syscall1(SYS_EXIT, code as u32);
    loop { core::arch::asm!("pause", options(nomem, nostack)); }
}

pub unsafe fn vfs_read_file_by_name(filename: *const u8, buffer: *mut u8,
                                    maximum: u32) -> i32 {
    syscall3(SYS_FAT_READ, filename as u32, buffer as u32, maximum)
}

pub unsafe fn vfs_write_file(filename: *const u8, data: *const u8,
                             length: u32) -> i32 {
    syscall3(SYS_FAT_WRITE, filename as u32, data as u32, length)
}

pub unsafe fn fat16_read_file_by_name(filename: *const u8, buffer: *mut u8,
                                      maximum: u32) -> i32 {
    vfs_read_file_by_name(filename, buffer, maximum)
}

pub unsafe fn fat16_write_file(filename: *const u8, data: *const u8,
                               length: u32) -> i32 {
    vfs_write_file(filename, data, length)
}

/* Rust compiler-emitted memory helpers stay inside the app image as well. */
#[no_mangle]
pub unsafe extern "C" fn memset(destination: *mut core::ffi::c_void,
                                 value: i32, length: usize) -> *mut core::ffi::c_void {
    let destination = destination.cast::<u8>();
    for index in 0..length {
        core::ptr::write_volatile(destination.add(index), value as u8);
    }
    destination.cast()
}

#[no_mangle]
pub unsafe extern "C" fn memcpy(destination: *mut core::ffi::c_void,
                                 source: *const core::ffi::c_void,
                                 length: usize) -> *mut core::ffi::c_void {
    let destination = destination.cast::<u8>();
    let source = source.cast::<u8>();
    for index in 0..length {
        core::ptr::write_volatile(destination.add(index),
                                  core::ptr::read_volatile(source.add(index)));
    }
    destination.cast()
}

#[no_mangle]
pub unsafe extern "C" fn memmove(destination: *mut core::ffi::c_void,
                                  source: *const core::ffi::c_void,
                                  length: usize) -> *mut core::ffi::c_void {
    let destination = destination.cast::<u8>();
    let source = source.cast::<u8>();
    if (destination as usize) <= (source as usize) {
        for index in 0..length {
            core::ptr::write_volatile(destination.add(index),
                                      core::ptr::read_volatile(source.add(index)));
        }
    } else {
        for index in (0..length).rev() {
            core::ptr::write_volatile(destination.add(index),
                                      core::ptr::read_volatile(source.add(index)));
        }
    }
    destination.cast()
}

#[no_mangle]
pub unsafe extern "C" fn memcmp(left: *const core::ffi::c_void,
                                 right: *const core::ffi::c_void,
                                 length: usize) -> i32 {
    let left = left.cast::<u8>();
    let right = right.cast::<u8>();
    for index in 0..length {
        let a = *left.add(index);
        let b = *right.add(index);
        if a != b { return a as i32 - b as i32; }
    }
    0
}

#[no_mangle]
pub unsafe extern "C" fn strlen(string: *const i8) -> usize {
    let mut length = 0;
    while core::ptr::read_volatile(string.add(length)) != 0 { length += 1; }
    length
}

#[no_mangle]
pub unsafe extern "C" fn strncmp(left: *const i8, right: *const i8,
                                  length: usize) -> i32 {
    for index in 0..length {
        let a = core::ptr::read_volatile(left.add(index)) as u8;
        let b = core::ptr::read_volatile(right.add(index)) as u8;
        if a != b || a == 0 { return a as i32 - b as i32; }
    }
    0
}

#[no_mangle]
pub unsafe extern "C" fn bcmp(left: *const core::ffi::c_void,
                               right: *const core::ffi::c_void,
                               length: usize) -> i32 {
    memcmp(left, right, length)
}

#[no_mangle]
pub unsafe extern "C" fn rust_eh_personality() {}
