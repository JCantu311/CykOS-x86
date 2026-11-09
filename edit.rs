#![no_std]
#![no_main]
#![allow(static_mut_refs)]
#![allow(dead_code)]
#![allow(unused_variables)]

use core::panic::PanicInfo;

// =========================================================================
// Kernel C ABI Imports
// =========================================================================
extern "C" {
    fn clear_screen();
    fn print_char(c: u8);
    fn print_string(s: *const u8);
    fn get_char() -> u8;
    fn has_key() -> i32;
    fn fat16_read_file_by_name(filename: *const u8, buffer: *mut u8, max_bytes: u32) -> i32;
    fn fat16_write_file(filename: *const u8, data: *const u8, size: u32) -> i32;
}

// =========================================================================
// Configuration & Constants
// =========================================================================
const VGA_MEM: *mut u8 = 0xB8000 as *mut u8;
const SCREEN_COLS: usize = 80;
const TEXT_ROWS: usize = 24; // Rows 0..23 for text editing; Row 24 for status & commands
const MAX_LINES: usize = 256;
const MAX_LINE_LEN: usize = 80;

#[derive(Copy, Clone, PartialEq)]
enum Mode {
    Normal,
    Insert,
    Command,
}

// Static storage: prevents stack overflows in freestanding environment
static mut LINES: [[u8; MAX_LINE_LEN]; MAX_LINES] = [[0; MAX_LINE_LEN]; MAX_LINES];
static mut LINE_LENS: [usize; MAX_LINES] = [0; MAX_LINES];
static mut NUM_LINES: usize = 1;

static mut FILENAME_BUF: [u8; 16] = [0; 16];
static mut CMD_BUF: [u8; 32] = [0; 32];
static mut CMD_LEN: usize = 0;
static mut STATUS_MSG: [u8; 48] = [0; 48];

// =========================================================================
// Hardware Cursor & VGA Direct Access
// =========================================================================
#[inline(always)]
fn set_cursor(col: usize, row: usize) {
    let pos = (row * SCREEN_COLS + col) as u16;
    unsafe {
        core::arch::asm!("out dx, al", in("dx") 0x3D4u16, in("al") 0x0Eu8, options(nomem, nostack));
        core::arch::asm!("out dx, al", in("dx") 0x3D5u16, in("al") ((pos >> 8) & 0xFF) as u8, options(nomem, nostack));
        core::arch::asm!("out dx, al", in("dx") 0x3D4u16, in("al") 0x0Fu8, options(nomem, nostack));
        core::arch::asm!("out dx, al", in("dx") 0x3D5u16, in("al") (pos & 0xFF) as u8, options(nomem, nostack));
    }
}

#[inline(always)]
fn vga_write_cell(col: usize, row: usize, ch: u8, attr: u8) {
    if col < SCREEN_COLS && row < 25 {
        let offset = (row * SCREEN_COLS + col) * 2;
        unsafe {
            *VGA_MEM.add(offset) = ch;
            *VGA_MEM.add(offset + 1) = attr;
        }
    }
}

fn set_status(msg: &[u8]) {
    unsafe {
        for i in 0..48 {
            STATUS_MSG[i] = 0;
        }
        let len = if msg.len() < 47 { msg.len() } else { 47 };
        for i in 0..len {
            STATUS_MSG[i] = msg[i];
        }
    }
}

// =========================================================================
// Screen Rendering
// =========================================================================
fn render(cursor_col: usize, cursor_row: usize, row_offset: usize, mode: Mode) {
    // 1. Render text rows (0..23)
    for screen_row in 0..TEXT_ROWS {
        let line_idx = row_offset + screen_row;
        let line_len = unsafe { if line_idx < NUM_LINES { LINE_LENS[line_idx] } else { 0 } };

        for col in 0..SCREEN_COLS {
            let (ch, attr) = if line_idx < unsafe { NUM_LINES } {
                if col < line_len {
                    (unsafe { LINES[line_idx][col] }, 0x07) // Light grey text
                } else {
                    (b' ', 0x07)
                }
            } else if col == 0 {
                (b'~', 0x09) // Vi-style unused line marker (Light Blue)
            } else {
                (b' ', 0x07)
            };

            vga_write_cell(col, screen_row, ch, attr);
        }
    }

    // 2. Render Row 24 (Status Bar / Command Line)
    let status_attr = 0x70; // Black text on Light Grey background
    for col in 0..SCREEN_COLS {
        vga_write_cell(col, 24, b' ', status_attr);
    }

    if mode == Mode::Command {
        vga_write_cell(0, 24, b':', 0x0F);
        unsafe {
            for i in 0..CMD_LEN {
                vga_write_cell(1 + i, 24, CMD_BUF[i], 0x0F);
            }
            set_cursor(1 + CMD_LEN, 24);
        }
    } else {
        let mode_str: &[u8] = match mode {
            Mode::Insert => b"-- INSERT -- ",
            Mode::Normal => b"NORMAL ",
            Mode::Command => b"",
        };

        let mut pos = 0;
        for i in 0..mode_str.len() {
            vga_write_cell(pos, 24, mode_str[i], status_attr);
            pos += 1;
        }

        // Active Filename
        unsafe {
            for i in 0..16 {
                let b = FILENAME_BUF[i];
                if b == 0 { break; }
                vga_write_cell(pos, 24, b, status_attr);
                pos += 1;
            }
        }

        // Status / error message if set
        unsafe {
            if STATUS_MSG[0] != 0 {
                vga_write_cell(pos, 24, b' ', status_attr); pos += 1;
                vga_write_cell(pos, 24, b'-', status_attr); pos += 1;
                vga_write_cell(pos, 24, b' ', status_attr); pos += 1;
                for i in 0..48 {
                    let b = STATUS_MSG[i];
                    if b == 0 { break; }
                    vga_write_cell(pos, 24, b, status_attr);
                    pos += 1;
                }
            }
        }

        let screen_r = if cursor_row >= row_offset { cursor_row - row_offset } else { 0 };
        set_cursor(cursor_col, screen_r);
    }
}

// =========================================================================
// File I/O
// =========================================================================
fn load_file(filename: *const u8) {
    static mut LOAD_BUF: [u8; 16384] = [0; 16384];
    unsafe {
        let bytes = fat16_read_file_by_name(filename, LOAD_BUF.as_mut_ptr(), 16384);
        if bytes <= 0 {
            set_status(b"[New File]");
            return;
        }

        NUM_LINES = 0;
        let mut cur_col = 0;
        let total = bytes as usize;

        for i in 0..total {
            let b = LOAD_BUF[i];
            if b == b'\r' {
                continue;
            } else if b == b'\n' {
                LINE_LENS[NUM_LINES] = cur_col;
                NUM_LINES += 1;
                cur_col = 0;
                if NUM_LINES >= MAX_LINES { break; }
            } else if cur_col < MAX_LINE_LEN {
                LINES[NUM_LINES][cur_col] = b;
                cur_col += 1;
            }
        }

        if cur_col > 0 || NUM_LINES == 0 {
            if NUM_LINES < MAX_LINES {
                LINE_LENS[NUM_LINES] = cur_col;
                NUM_LINES += 1;
            }
        }

        set_status(b"[Loaded]");
    }
}

fn save_file() -> bool {
    static mut SAVE_BUF: [u8; 16384] = [0; 16384];
    unsafe {
        let mut total_bytes = 0;
        for line_idx in 0..NUM_LINES {
            let len = LINE_LENS[line_idx];
            for col in 0..len {
                if total_bytes < 16380 {
                    SAVE_BUF[total_bytes] = LINES[line_idx][col];
                    total_bytes += 1;
                }
            }
            if line_idx + 1 < NUM_LINES && total_bytes < 16380 {
                SAVE_BUF[total_bytes] = b'\r';
                total_bytes += 1;
                SAVE_BUF[total_bytes] = b'\n';
                total_bytes += 1;
            }
        }

        let res = fat16_write_file(FILENAME_BUF.as_ptr(), SAVE_BUF.as_ptr(), total_bytes as u32);
        if res == 0 {
            set_status(b"File written.");
            true
        } else {
            set_status(b"Error: Write failed!");
            false
        }
    }
}

// =========================================================================
// Main Vi-style Editor Loop
// =========================================================================
fn run_editor() -> i32 {
    let mut cursor_row: usize = 0;
    let mut cursor_col: usize = 0;
    let mut row_offset: usize = 0;
    let mut mode = Mode::Normal;

    loop {
        // Adjust vertical scrolling window
        if cursor_row < row_offset {
            row_offset = cursor_row;
        } else if cursor_row >= row_offset + TEXT_ROWS {
            row_offset = cursor_row - TEXT_ROWS + 1;
        }

        render(cursor_col, cursor_row, row_offset, mode);

        let key = unsafe { get_char() };

        match mode {
            Mode::Normal => {
                match key {
                    b'i' => { // Enter Insert mode before cursor
                        mode = Mode::Insert;
                        set_status(b"");
                    }
                    b'a' => { // Append after cursor
                        mode = Mode::Insert;
                        let max_col = unsafe { LINE_LENS[cursor_row] };
                        if cursor_col < max_col && cursor_col + 1 < MAX_LINE_LEN {
                            cursor_col += 1;
                        }
                        set_status(b"");
                    }
                    b'o' => { // Open newline below and insert
                        unsafe {
                            if NUM_LINES < MAX_LINES {
                                let mut r = NUM_LINES;
                                while r > cursor_row + 1 {
                                    LINE_LENS[r] = LINE_LENS[r - 1];
                                    for c in 0..MAX_LINE_LEN {
                                        LINES[r][c] = LINES[r - 1][c];
                                    }
                                    r -= 1;
                                }
                                LINE_LENS[cursor_row + 1] = 0;
                                for c in 0..MAX_LINE_LEN {
                                    LINES[cursor_row + 1][c] = 0;
                                }
                                NUM_LINES += 1;
                                cursor_row += 1;
                                cursor_col = 0;
                                mode = Mode::Insert;
                                set_status(b"");
                            }
                        }
                    }
                    b'O' => { // Open newline above and insert
                        unsafe {
                            if NUM_LINES < MAX_LINES {
                                let mut r = NUM_LINES;
                                while r > cursor_row {
                                    LINE_LENS[r] = LINE_LENS[r - 1];
                                    for c in 0..MAX_LINE_LEN {
                                        LINES[r][c] = LINES[r - 1][c];
                                    }
                                    r -= 1;
                                }
                                LINE_LENS[cursor_row] = 0;
                                for c in 0..MAX_LINE_LEN {
                                    LINES[cursor_row][c] = 0;
                                }
                                NUM_LINES += 1;
                                cursor_col = 0;
                                mode = Mode::Insert;
                                set_status(b"");
                            }
                        }
                    }
                    b':' => { // Enter Command mode
                        mode = Mode::Command;
                        unsafe { CMD_LEN = 0; }
                    }
                    b'h' => { // Left
                        if cursor_col > 0 { cursor_col -= 1; }
                    }
                    b'l' => { // Right
                        let max_col = unsafe { LINE_LENS[cursor_row] };
                        if cursor_col < max_col && cursor_col + 1 < MAX_LINE_LEN {
                            cursor_col += 1;
                        }
                    }
                    b'j' => { // Down
                        if cursor_row + 1 < unsafe { NUM_LINES } {
                            cursor_row += 1;
                            let max_col = unsafe { LINE_LENS[cursor_row] };
                            if cursor_col > max_col { cursor_col = max_col; }
                        }
                    }
                    b'k' => { // Up
                        if cursor_row > 0 {
                            cursor_row -= 1;
                            let max_col = unsafe { LINE_LENS[cursor_row] };
                            if cursor_col > max_col { cursor_col = max_col; }
                        }
                    }
                    b'0' => { // Beginning of line
                        cursor_col = 0;
                    }
                    b'$' => { // End of line
                        cursor_col = unsafe { LINE_LENS[cursor_row] };
                    }
                    b'x' => { // Delete character under cursor
                        unsafe {
                            let len = LINE_LENS[cursor_row];
                            if cursor_col < len {
                                for c in cursor_col..(len - 1) {
                                    LINES[cursor_row][c] = LINES[cursor_row][c + 1];
                                }
                                LINE_LENS[cursor_row] -= 1;
                            }
                        }
                    }
                    b'd' => { // Delete line
                        unsafe {
                            if NUM_LINES > 1 {
                                for r in cursor_row..(NUM_LINES - 1) {
                                    LINE_LENS[r] = LINE_LENS[r + 1];
                                    for c in 0..MAX_LINE_LEN {
                                        LINES[r][c] = LINES[r + 1][c];
                                    }
                                }
                                NUM_LINES -= 1;
                                if cursor_row >= NUM_LINES && cursor_row > 0 {
                                    cursor_row -= 1;
                                }
                                cursor_col = 0;
                            } else {
                                LINE_LENS[0] = 0;
                                cursor_col = 0;
                            }
                        }
                    }
                    _ => {}
                }
            }

            Mode::Insert => {
                if key == 0x1B { // Escape -> Normal Mode
                    mode = Mode::Normal;
                    let max_col = unsafe { LINE_LENS[cursor_row] };
                    if cursor_col > 0 && cursor_col >= max_col {
                        cursor_col = if max_col > 0 { max_col - 1 } else { 0 };
                    }
                } else if key == 0x08 { // Backspace
                    unsafe {
                        if cursor_col > 0 {
                            let len = LINE_LENS[cursor_row];
                            for c in (cursor_col - 1)..(len - 1) {
                                LINES[cursor_row][c] = LINES[cursor_row][c + 1];
                            }
                            LINE_LENS[cursor_row] -= 1;
                            cursor_col -= 1;
                        } else if cursor_row > 0 {
                            // Merge with line above
                            let prev_len = LINE_LENS[cursor_row - 1];
                            let cur_len = LINE_LENS[cursor_row];
                            if prev_len + cur_len < MAX_LINE_LEN {
                                for c in 0..cur_len {
                                    LINES[cursor_row - 1][prev_len + c] = LINES[cursor_row][c];
                                }
                                LINE_LENS[cursor_row - 1] += cur_len;

                                for r in cursor_row..(NUM_LINES - 1) {
                                    LINE_LENS[r] = LINE_LENS[r + 1];
                                    for c in 0..MAX_LINE_LEN {
                                        LINES[r][c] = LINES[r + 1][c];
                                    }
                                }
                                NUM_LINES -= 1;
                                cursor_row -= 1;
                                cursor_col = prev_len;
                            }
                        }
                    }
                } else if key == b'\n' || key == b'\r' { // Enter -> Newline
                    unsafe {
                        if NUM_LINES < MAX_LINES {
                            let mut r = NUM_LINES;
                            while r > cursor_row + 1 {
                                LINE_LENS[r] = LINE_LENS[r - 1];
                                for c in 0..MAX_LINE_LEN {
                                    LINES[r][c] = LINES[r - 1][c];
                                }
                                r -= 1;
                            }
                            let cur_len = LINE_LENS[cursor_row];
                            let split_len = if cur_len >= cursor_col { cur_len - cursor_col } else { 0 };
                            LINE_LENS[cursor_row] = cursor_col;
                            LINE_LENS[cursor_row + 1] = split_len;

                            for c in 0..split_len {
                                LINES[cursor_row + 1][c] = LINES[cursor_row][cursor_col + c];
                            }

                            NUM_LINES += 1;
                            cursor_row += 1;
                            cursor_col = 0;
                        }
                    }
                } else if key >= 32 && key <= 126 { // Printable character
                    unsafe {
                        let len = LINE_LENS[cursor_row];
                        if len < MAX_LINE_LEN - 1 {
                            let mut c = len;
                            while c > cursor_col {
                                LINES[cursor_row][c] = LINES[cursor_row][c - 1];
                                c -= 1;
                            }
                            LINES[cursor_row][cursor_col] = key;
                            LINE_LENS[cursor_row] += 1;
                            cursor_col += 1;
                        }
                    }
                }
            }

            Mode::Command => {
                if key == 0x1B { // Escape cancels Command mode
                    mode = Mode::Normal;
                } else if key == 0x08 { // Backspace
                    unsafe {
                        if CMD_LEN > 0 {
                            CMD_LEN -= 1;
                        } else {
                            mode = Mode::Normal;
                        }
                    }
                } else if key == b'\n' || key == b'\r' { // Execute command
                    let is_w = unsafe { CMD_LEN == 1 && CMD_BUF[0] == b'w' };
                    let is_q = unsafe { (CMD_LEN == 1 && CMD_BUF[0] == b'q') || (CMD_LEN == 2 && CMD_BUF[0] == b'q' && CMD_BUF[1] == b'!') };
                    let is_wq = unsafe { CMD_LEN == 2 && CMD_BUF[0] == b'w' && CMD_BUF[1] == b'q' };

                    if is_w {
                        save_file();
                        mode = Mode::Normal;
                    } else if is_q {
                        unsafe { clear_screen(); }
                        return 0; // Exit back to shell
                    } else if is_wq {
                        if save_file() {
                            unsafe { clear_screen(); }
                            return 0;
                        }
                        mode = Mode::Normal;
                    } else {
                        set_status(b"Unknown command");
                        mode = Mode::Normal;
                    }
                } else if key >= 32 && key <= 126 {
                    unsafe {
                        if CMD_LEN < CMD_BUF.len() - 1 {
                            CMD_BUF[CMD_LEN] = key;
                            CMD_LEN += 1;
                        }
                    }
                }
            }
        }
    }
}

// =========================================================================
// Application Entry Points (Level 4 Userland)
// =========================================================================

// CRITICAL: This attribute pins 'main' into the ".entry" section.
// user.ld places KEEP(*(.entry)) at byte 0 of the binary so the kernel
// can call directly into (void*)0x50000 without any indirection.
#[no_mangle]
#[link_section = ".entry"]
pub extern "C" fn main(argc: isize, argv: *const *const u8) -> i32 {
    unsafe {
        // Reset state
        for l in 0..MAX_LINES {
            LINE_LENS[l] = 0;
            for c in 0..MAX_LINE_LEN {
                LINES[l][c] = 0;
            }
        }
        NUM_LINES = 1;
        CMD_LEN = 0;
        for i in 0..16 { FILENAME_BUF[i] = 0; }
        for i in 0..48 { STATUS_MSG[i] = 0; }

        // Read filename argument from argv[1] if passed
        let mut found_arg = false;
        if argc > 1 && !argv.is_null() {
            let arg1 = *argv.add(1);
            if !arg1.is_null() {
                let mut idx = 0;
                while *arg1.add(idx) != 0 && idx < 12 {
                    FILENAME_BUF[idx] = *arg1.add(idx);
                    idx += 1;
                }
                if idx > 0 {
                    found_arg = true;
                }
            }
        }

        // Default filename if not specified
        if !found_arg {
            let def = b"UNTITLED.TXT";
            for i in 0..def.len() {
                FILENAME_BUF[i] = def[i];
            }
        }

        load_file(FILENAME_BUF.as_ptr());
    }

    run_editor()
}

// Dual entry point compatibility
#[no_mangle]
pub extern "C" fn user_edit(argc: isize, argv: *const *const u8) -> i32 {
    main(argc, argv)
}

// Panic Handler for freestanding environment
#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {
        unsafe {
            core::arch::asm!("hlt");
        }
    }
}
