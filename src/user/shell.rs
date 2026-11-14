#![no_std]
#![no_main]

use core::panic::PanicInfo;

// Navigation & Special Key Codes
const KEY_UP: u8       = 0x80;
const KEY_DOWN: u8     = 0x81;
const KEY_LEFT: u8     = 0x82;
const KEY_RIGHT: u8    = 0x83;
const KEY_HOME: u8     = 0x84;
const KEY_END: u8      = 0x85;
const KEY_PAGEUP: u8   = 0x86;
const KEY_PAGEDOWN: u8 = 0x87;
const KEY_DELETE: u8   = 0x89;

// Level 1 Kernel & Driver C Function Declarations
extern "C" {
    fn clear_screen();
    #[link_name = "print_char"]
    fn kernel_print_char(c: u8);
    fn print_string(s: *const u8);
    #[link_name = "print_color_string"]
    fn kernel_print_color_string(s: *const u8, color: u8);
    fn get_char() -> u8;
    fn has_key() -> i32;
    fn secwait(seconds: u32);
    fn outb(port: u16, data: u8);
    fn outw(port: u16, data: u16);
    fn get_cursor_pos(x: *mut i32, y: *mut i32);
    fn set_cursor_pos(x: i32, y: i32);
    fn vfs_list_directory(path: *const u8, callback: extern "C" fn(*const u8, u32, u8)) -> i32;
    fn vfs_delete_file(filename: *const u8) -> i32;
    fn vfs_read_file_by_name(filename: *const u8, buffer: *mut u8, max_bytes: u32) -> i32;
    fn vfs_write_file(filename: *const u8, data: *const u8, size: u32) -> i32;
    fn vfs_mkdir(path: *const u8) -> i32;
    fn vfs_rmdir(path: *const u8) -> i32;
    fn vfs_chdir(path: *const u8) -> i32;
    fn vfs_getcwd(buffer: *mut u8, max_bytes: u32) -> i32;

    // External user command support (usercmd.c)
    fn exec_user_command(cmd_name: *const u8, args: *const u8) -> i32;
    fn list_user_commands();

    // Temporary in-memory standard input/output for synchronous pipelines.
    fn syscall_pipe_begin(input: *const u8, input_len: u32, output: *mut u8, output_capacity: u32);
    fn syscall_pipe_end() -> u32;
    fn syscall_pipe_write(data: *const u8, length: u32) -> u32;
    fn syscall_pipe_read(data: *mut u8, length: u32) -> i32;
}

// Panic Handler for freestanding environment
#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    puts(b"\n[KERNEL PANIC] CykShell crashed.\n");
    loop {
        unsafe {
            core::arch::asm!("hlt");
        }
    }
}

// Low-Level I/O Helpers
static mut PIPE_CAPTURE_ACTIVE: bool = false;

fn print_char(c: u8) {
    unsafe {
        if PIPE_CAPTURE_ACTIVE {
            syscall_pipe_write(&c as *const u8, 1);
        } else {
            kernel_print_char(c);
        }
    }
}

fn print_color_string(s: *const u8, color: u8) {
    if s.is_null() {
        return;
    }
    unsafe {
        if PIPE_CAPTURE_ACTIVE {
            let mut index = 0;
            while *s.add(index) != 0 {
                syscall_pipe_write(s.add(index), 1);
                index += 1;
            }
        } else {
            kernel_print_color_string(s, color);
        }
    }
}

fn puts(s: &[u8]) {
    for &b in s {
        unsafe {
            print_char(b);
        }
    }
}

fn puts_color(s: &[u8], color: u8) {
    for &b in s {
        unsafe {
            let null_term = [b, 0];
            print_color_string(null_term.as_ptr(), color);
        }
    }
}

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

fn bytes_eq(a: &[u8], b: &[u8]) -> bool {
    if a.len() != b.len() {
        return false;
    }
    let mut i = 0;
    while i < a.len() {
        if a[i] != b[i] {
            return false;
        }
        i += 1;
    }
    true
}

// Bounds-safe trimming of ASCII whitespace without slice arithmetic
fn trim_slice<'a>(mut s: &'a [u8]) -> &'a [u8] {
    while let Some((&first, rest)) = s.split_first() {
        if first == b' ' || first == b'\t' || first == b'\r' || first == b'\n' {
            s = rest;
        } else {
            break;
        }
    }
    while let Some((&last, rest)) = s.split_last() {
        if last == b' ' || last == b'\t' || last == b'\r' || last == b'\n' {
            s = rest;
        } else {
            break;
        }
    }
    s
}

// =========================================================================
// Command Registry & Handlers (Scalable Command Architecture)
// =========================================================================

pub struct Command {
    pub name: &'static [u8],
    pub description: &'static [u8],
    pub handler: fn(&[u8]),
}

// Built-in Command: clear (Linux compatible screen clear)
fn cmd_clear(_args: &[u8]) {
    unsafe {
        clear_screen();
    }
}

// Callback invoked for each directory entry by Level 1 FAT16 driver
static mut DIR_FILE_COUNT: u32 = 0;

extern "C" fn dir_entry_callback(name_ptr: *const u8, size: u32, is_dir: u8) {
    if name_ptr.is_null() {
        return;
    }

    unsafe {
        DIR_FILE_COUNT += 1;
        let mut len = 0;
        while *name_ptr.add(len) != 0 && len < 13 {
            print_char(*name_ptr.add(len));
            len += 1;
        }
        while len < 16 {
            print_char(b' ');
            len += 1;
        }
    }

    if is_dir != 0 {
        puts(b"<DIR>\n");
    } else {
        print_u32(size);
        puts(b" bytes\n");
    }
}

// Built-in Command: dir (MS-DOS compatible directory listing)
fn cmd_dir(_args: &[u8]) {
    puts(b"\n Volume in root is CYKOS_DISK\n");
    puts(b" Directory of ");
    let mut buf = [0u8; 128];
    let len = unsafe { vfs_getcwd(buf.as_mut_ptr(), 128) };
    if len > 0 {
        let ulen = len as usize;
        for i in 0..ulen {
            unsafe { print_char(buf[i]); }
        }
    } else {
        puts(b"/");
    }
    puts(b"\n\n");

    unsafe {
        DIR_FILE_COUNT = 0;
        if vfs_list_directory(core::ptr::null(), dir_entry_callback) < 0 {
            puts(b"Directory read failed.\n");
            return;
        }
    }

    puts(b"\n               ");
    unsafe { print_u32(DIR_FILE_COUNT); }
    puts(b" File(s)\n\n");
}

// Built-in Command: reboot (Linux compatible system reboot)
fn cmd_reboot(_args: &[u8]) {
    puts(b"\nRebooting CykOS...\n");
    unsafe {
        secwait(1);

        // 1. Standard 8042 PS/2 controller CPU RESET pulse
        outb(0x64, 0xFE);

        // 2. Fallback: Triple fault via invalid IDT
        core::arch::asm!(
            "lidt [{}]",
            "int3",
            in(reg) 0,
            options(noreturn)
        );
    }
}

// Built-in Command: shutdown (Linux compatible system poweroff)
fn cmd_shutdown(_args: &[u8]) {
    puts(b"\nShutting down CykOS...\n");
    unsafe {
        secwait(1);

        // 1. QEMU ACPI poweroff port
        outw(0x604, 0x2000);

        // 2. Bochs / VirtualBox / older QEMU poweroff port
        outw(0xB004, 0x2000);

        // 3. APM poweroff port
        outw(0x4004, 0x3400);

        // 4. Hardware fallback
        puts(b"It is now safe to turn off your computer.\n");
        loop {
            core::arch::asm!("cli", "hlt");
        }
    }
}

// Built-in Command: rm / del (Delete file from FAT16)
fn cmd_rm(args: &[u8]) {
    let filename = trim_slice(args);
    if filename.is_empty() {
        puts(b"Usage: rm <filename>\n");
        return;
    }

    let mut fname_buf = [0u8; 64];
    let len = if filename.len() < 63 { filename.len() } else { 63 };
    let mut i = 0;
    while i < len {
        fname_buf[i] = filename[i];
        i += 1;
    }
    fname_buf[len] = 0;

    let res = unsafe { vfs_delete_file(fname_buf.as_ptr()) };
    if res == 0 {
        puts(b"Deleted file: ");
        puts(filename);
        puts(b"\n");
    } else if res == -3 {
        puts(b"rm: file not found: ");
        puts(filename);
        puts(b"\n");
    } else if res == -4 {
        puts(b"rm: cannot delete directory: ");
        puts(filename);
        puts(b"\n");
    } else {
        puts(b"rm: failed to delete file.\n");
    }
}

// Static file buffer in .bss for shell file operations (32 KB)
static mut SHELL_FILE_BUF: [u8; 32768] = [0; 32768];
static mut SCRIPT_BUF: [u8; 32768] = [0; 32768];
static mut SCRIPT_ACTIVE: bool = false;
static mut PIPE_BUFFERS: [[u8; 32768]; 2] = [[0; 32768]; 2];

// Built-in Command: touch (Create or touch an empty file)
fn cmd_touch(args: &[u8]) {
    let filename = trim_slice(args);
    if filename.is_empty() {
        puts(b"Usage: touch <filename>\n");
        return;
    }

    let mut fname_buf = [0u8; 64];
    let len = if filename.len() < 63 { filename.len() } else { 63 };
    let mut i = 0;
    while i < len {
        fname_buf[i] = filename[i];
        i += 1;
    }
    fname_buf[len] = 0;

    unsafe {
        let mut check_buf = [0u8; 1];
        let exists = vfs_read_file_by_name(fname_buf.as_ptr(), check_buf.as_mut_ptr(), 1);
        if exists < 0 {
            let dummy = 0u8;
            let res = vfs_write_file(fname_buf.as_ptr(), &dummy, 0);
            if res == 0 {
                puts(b"Created: ");
                puts(filename);
                puts(b"\n");
            } else {
                puts(b"touch: failed to create file.\n");
            }
        } else {
            puts(b"touch: file already exists: ");
            puts(filename);
            puts(b"\n");
        }
    }
}

// Built-in Command: cp (Copy file)
fn cmd_cp(args: &[u8]) {
    let trimmed = trim_slice(args);
    if trimmed.is_empty() {
        puts(b"Usage: cp <source> <destination>\n");
        return;
    }

    let mut split_idx = 0;
    while split_idx < trimmed.len() && trimmed[split_idx] != b' ' {
        split_idx += 1;
    }

    if split_idx >= trimmed.len() {
        puts(b"Usage: cp <source> <destination>\n");
        return;
    }

    let src = &trimmed[..split_idx];
    let dst = trim_slice(&trimmed[split_idx..]);

    if dst.is_empty() {
        puts(b"Usage: cp <source> <destination>\n");
        return;
    }

    let mut src_buf = [0u8; 64];
    let src_len = if src.len() < 63 { src.len() } else { 63 };
    let mut i = 0;
    while i < src_len {
        src_buf[i] = src[i];
        i += 1;
    }
    src_buf[src_len] = 0;

    let mut dst_buf = [0u8; 64];
    let dst_len = if dst.len() < 63 { dst.len() } else { 63 };
    i = 0;
    while i < dst_len {
        dst_buf[i] = dst[i];
        i += 1;
    }
    dst_buf[dst_len] = 0;

    unsafe {
        let bytes_read = vfs_read_file_by_name(src_buf.as_ptr(), SHELL_FILE_BUF.as_mut_ptr(), 32768);
        if bytes_read < 0 {
            puts(b"cp: cannot open source file: ");
            puts(src);
            puts(b"\n");
            return;
        }

        let res = vfs_write_file(dst_buf.as_ptr(), SHELL_FILE_BUF.as_ptr(), bytes_read as u32);
        if res == 0 {
            puts(b"Copied ");
            puts(src);
            puts(b" -> ");
            puts(dst);
            puts(b" (");
            print_u32(bytes_read as u32);
            puts(b" bytes)\n");
        } else {
            puts(b"cp: failed to write destination file: ");
            puts(dst);
            puts(b"\n");
        }
    }
}

// Built-in Command: wc (Count lines, words, and bytes)
fn cmd_wc(args: &[u8]) {
    let filename = trim_slice(args);
    let bytes_read = unsafe {
        if filename.is_empty() {
            syscall_pipe_read(SHELL_FILE_BUF.as_mut_ptr(), 32768)
        } else {
            let mut fname_buf = [0u8; 64];
            let len = if filename.len() < 63 { filename.len() } else { 63 };
            let mut i = 0;
            while i < len {
                fname_buf[i] = filename[i];
                i += 1;
            }
            fname_buf[len] = 0;
            vfs_read_file_by_name(fname_buf.as_ptr(), SHELL_FILE_BUF.as_mut_ptr(), 32768)
        }
    };
    if bytes_read < 0 {
        if filename.is_empty() {
            puts(b"Usage: wc <filename> (or pipe input)\n");
        } else {
            puts(b"wc: cannot open file: ");
            puts(filename);
            puts(b"\n");
        }
        return;
    }

    unsafe {
        let total = bytes_read as usize;
        let mut lines: u32 = 0;
        let mut words: u32 = 0;
        let mut in_word = false;

        let mut idx = 0;
        while idx < total {
            let b = SHELL_FILE_BUF[idx];
            if b == b'\n' {
                lines += 1;
            }
            if b == b' ' || b == b'\t' || b == b'\n' || b == b'\r' {
                in_word = false;
            } else if !in_word {
                in_word = true;
                words += 1;
            }
            idx += 1;
        }

        puts(b"  ");
        print_u32(lines);
        puts(b"  ");
        print_u32(words);
        puts(b"  ");
        print_u32(total as u32);
        puts(b"  ");
        puts(filename);
        puts(b"\n");
    }
}

// Built-in Command: hexdump (Canonical hex dump)
fn cmd_hexdump(args: &[u8]) {
    let filename = trim_slice(args);
    if filename.is_empty() {
        puts(b"Usage: hexdump <filename>\n");
        return;
    }

    let mut fname_buf = [0u8; 64];
    let len = if filename.len() < 63 { filename.len() } else { 63 };
    let mut i = 0;
    while i < len {
        fname_buf[i] = filename[i];
        i += 1;
    }
    fname_buf[len] = 0;

    unsafe {
        let bytes_read = vfs_read_file_by_name(fname_buf.as_ptr(), SHELL_FILE_BUF.as_mut_ptr(), 16384);
        if bytes_read < 0 {
            puts(b"hexdump: cannot open file: ");
            puts(filename);
            puts(b"\n");
            return;
        }

        let total = bytes_read as usize;
        let mut offset = 0;

        while offset < total {
            print_hex_u32(offset as u32);
            puts(b": ");

            let mut j = 0;
            while j < 16 {
                if offset + j < total {
                    let b = SHELL_FILE_BUF[offset + j];
                    print_hex_byte(b);
                    print_char(b' ');
                } else {
                    puts(b"   ");
                }
                if j == 7 {
                    print_char(b' ');
                }
                j += 1;
            }

            puts(b" |");
            j = 0;
            while j < 16 {
                if offset + j < total {
                    let b = SHELL_FILE_BUF[offset + j];
                    if b >= 32 && b <= 126 {
                        print_char(b);
                    } else {
                        print_char(b'.');
                    }
                }
                j += 1;
            }
            puts(b"|\n");

            offset += 16;
        }
    }
}

// Built-in Command: mkdir (Create directory)
fn cmd_mkdir(args: &[u8]) {
    let dirname = trim_slice(args);
    if dirname.is_empty() {
        puts(b"Usage: mkdir <directory>\n");
        return;
    }

    let mut dname_buf = [0u8; 64];
    let len = if dirname.len() < 63 { dirname.len() } else { 63 };
    let mut i = 0;
    while i < len {
        dname_buf[i] = dirname[i];
        i += 1;
    }
    dname_buf[len] = 0;

    let res = unsafe { vfs_mkdir(dname_buf.as_ptr()) };
    if res == 0 {
        puts(b"Created directory: ");
        puts(dirname);
        puts(b"\n");
    } else if res == -3 {
        puts(b"mkdir: directory already exists: ");
        puts(dirname);
        puts(b"\n");
    } else if res == -4 {
        puts(b"mkdir: disk full\n");
    } else {
        puts(b"mkdir: failed to create directory.\n");
    }
}

// Built-in Command: rmdir (Remove empty directory)
fn cmd_rmdir(args: &[u8]) {
    let dirname = trim_slice(args);
    if dirname.is_empty() {
        puts(b"Usage: rmdir <directory>\n");
        return;
    }

    let mut dname_buf = [0u8; 64];
    let len = if dirname.len() < 63 { dirname.len() } else { 63 };
    let mut i = 0;
    while i < len {
        dname_buf[i] = dirname[i];
        i += 1;
    }
    dname_buf[len] = 0;

    let res = unsafe { vfs_rmdir(dname_buf.as_ptr()) };
    if res == 0 {
        puts(b"Removed directory: ");
        puts(dirname);
        puts(b"\n");
    } else if res == -3 {
        puts(b"rmdir: directory not found: ");
        puts(dirname);
        puts(b"\n");
    } else if res == -4 {
        puts(b"rmdir: not a directory: ");
        puts(dirname);
        puts(b"\n");
    } else if res == -5 {
        puts(b"rmdir: cannot delete root directory\n");
    } else if res == -6 {
        puts(b"rmdir: directory not empty: ");
        puts(dirname);
        puts(b"\n");
    } else {
        puts(b"rmdir: failed to remove directory.\n");
    }
}

// Built-in Command: cd (Change current working directory)
fn cmd_cd(args: &[u8]) {
    let path = trim_slice(args);
    let mut path_buf = [0u8; 64];
    let len = if path.len() < 63 { path.len() } else { 63 };
    let mut i = 0;
    while i < len {
        path_buf[i] = path[i];
        i += 1;
    }
    path_buf[len] = 0;

    let res = unsafe {
        if len == 0 {
            let root = [b'/', 0];
            vfs_chdir(root.as_ptr())
        } else {
            vfs_chdir(path_buf.as_ptr())
        }
    };

    if res != 0 {
        puts(b"cd: directory not found: ");
        puts(path);
        puts(b"\n");
    }
}

// Built-in Command: pwd (Print current working directory)
fn cmd_pwd(_args: &[u8]) {
    let mut buf = [0u8; 128];
    let len = unsafe { vfs_getcwd(buf.as_mut_ptr(), 128) };
    if len >= 0 {
        let ulen = len as usize;
        for i in 0..ulen {
            unsafe { print_char(buf[i]); }
        }
        puts(b"\n");
    } else {
        puts(b"pwd: error getting current directory\n");
    }
}

// =========================================================================
// Built-in Command: ls (Linux-style compact directory listing)
// =========================================================================

static mut LS_COL_OFFSET: usize = 0;

extern "C" fn ls_entry_callback(name_ptr: *const u8, _size: u32, is_dir: u8) {
    if name_ptr.is_null() {
        return;
    }

    unsafe {
        let mut name_len = 0;
        while *name_ptr.add(name_len) != 0 && name_len < 13 {
            name_len += 1;
        }

        // Each entry occupies its display width (name + 1 for trailing '/' if dir) + 2 spaces padding
        let entry_width = name_len + (if is_dir != 0 { 1 } else { 0 });
        let total_needed = entry_width + 2;

        // If printing this entry would exceed screen width (80 columns), wrap to new line
        if LS_COL_OFFSET > 0 && LS_COL_OFFSET + total_needed > 80 {
            print_char(b'\n');
            LS_COL_OFFSET = 0;
        }

        if is_dir != 0 {
            // Print directory name in green with trailing /
            let mut i = 0;
            while i < name_len {
                let pair = [*name_ptr.add(i), 0];
                print_color_string(pair.as_ptr(), 0x0A);
                i += 1;
            }
            let slash = [b'/', 0];
            print_color_string(slash.as_ptr(), 0x0A);
        } else {
            let mut i = 0;
            while i < name_len {
                print_char(*name_ptr.add(i));
                i += 1;
            }
        }

        // Add 2 spaces between columns
        print_char(b' ');
        print_char(b' ');
        LS_COL_OFFSET += total_needed;
    }
}

fn cmd_ls(_args: &[u8]) {
    unsafe {
        LS_COL_OFFSET = 0;
        if vfs_list_directory(core::ptr::null(), ls_entry_callback) < 0 {
            puts(b"ls: directory read failed\n");
        } else if LS_COL_OFFSET > 0 {
            print_char(b'\n');
        }
    }
}

// =========================================================================
// Built-in Command: less (Full-screen file pager)
// =========================================================================

// VGA direct-write helper for status bar rendering
#[inline(always)]
fn vga_write(col: usize, row: usize, ch: u8, attr: u8) {
    if col < 80 && row < 25 {
        let offset = (row * 80 + col) * 2;
        unsafe {
            let ptr = 0xB8000 as *mut u8;
            *ptr.add(offset) = ch;
            *ptr.add(offset + 1) = attr;
        }
    }
}

// Pre-parsed line start offsets for the pager (supports files up to ~1024 lines)
static mut LESS_LINE_STARTS: [u32; 1024] = [0; 1024];
static mut LESS_LINE_COUNT: usize = 0;

fn cmd_less(args: &[u8]) {
    let target = trim_slice(args);
    let mut display_name = target;
    let mut bytes_read;
    if target.is_empty() {
        bytes_read = unsafe { syscall_pipe_read(SHELL_FILE_BUF.as_mut_ptr(), 32768) };
        if bytes_read < 0 {
            puts(b"Usage: less <filename|command> (or pipe input)\n");
            return;
        }
        display_name = b"pipe input";
    } else {
        bytes_read = -1;
        if target.len() < 64 {
            let mut fname_buf = [0u8; 64];
            for i in 0..target.len() {
                fname_buf[i] = target[i];
            }
            fname_buf[target.len()] = 0;
            bytes_read = unsafe {
                vfs_read_file_by_name(fname_buf.as_ptr(), SHELL_FILE_BUF.as_mut_ptr(), 32768)
            };
        }
    }

    if !target.is_empty() && bytes_read < 0 {
        let mut command_name_end = 0;
        while command_name_end < target.len() &&
            target[command_name_end] != b' ' && target[command_name_end] != b'\t' {
            command_name_end += 1;
        }
        let command_name = &target[..command_name_end];
        if bytes_eq(command_name, b"less") || bytes_eq(command_name, b"LESS") {
            puts(b"less: cannot page another less command\n");
            return;
        }

        unsafe {
            syscall_pipe_begin(core::ptr::null(), 0, PIPE_BUFFERS[0].as_mut_ptr(), PIPE_BUFFER_CAPACITY as u32);
            PIPE_CAPTURE_ACTIVE = true;
        }
        let command_found = dispatch_single_command(target);
        let command_output_len = unsafe {
            PIPE_CAPTURE_ACTIVE = false;
            syscall_pipe_end() as usize
        };
        if !command_found {
            puts(b"less: cannot open file or run command: ");
            puts(target);
            puts(b"\n");
            return;
        }
        unsafe {
            for i in 0..command_output_len {
                SHELL_FILE_BUF[i] = PIPE_BUFFERS[0][i];
            }
        }
        bytes_read = command_output_len as i32;
        display_name = b"command output";
    }

    let total = bytes_read as usize;
    if total == 0 {
        puts(b"(empty file)\n");
        return;
    }

    // Pre-scan line start offsets
    unsafe {
        LESS_LINE_STARTS[0] = 0;
        LESS_LINE_COUNT = 1;

        let mut idx = 0;
        while idx < total {
            if SHELL_FILE_BUF[idx] == b'\n' && idx + 1 < total && LESS_LINE_COUNT < 1024 {
                LESS_LINE_STARTS[LESS_LINE_COUNT] = (idx + 1) as u32;
                LESS_LINE_COUNT += 1;
            }
            idx += 1;
        }
    }

    let page_lines: usize = 24; // 24 rows for content, row 24 for status bar
    let mut scroll: usize = 0;

    loop {
        // --- Render page ---
        unsafe { clear_screen(); }

        unsafe {
            let mut row: usize = 0;
            while row < page_lines {
                let line_idx = scroll + row;
                if line_idx >= LESS_LINE_COUNT {
                    // Past end of file: show ~ markers
                    let tilde = [b'~', 0];
                    print_color_string(tilde.as_ptr(), 0x09); // Light Blue
                    print_char(b'\n');
                } else {
                    let start = LESS_LINE_STARTS[line_idx] as usize;
                    let end = if line_idx + 1 < LESS_LINE_COUNT {
                        LESS_LINE_STARTS[line_idx + 1] as usize
                    } else {
                        total
                    };

                    let mut col: usize = 0;
                    let mut bi = start;
                    while bi < end {
                        let b = SHELL_FILE_BUF[bi];
                        if b == b'\r' {
                            bi += 1;
                            continue;
                        }
                        if b == b'\n' {
                            break;
                        }
                        if b == b'\t' {
                            // Expand tab to spaces (next multiple of 8)
                            let next_tab = (col + 8) & !7;
                            while col < next_tab && col < 80 {
                                print_char(b' ');
                                col += 1;
                            }
                        } else if col < 80 {
                            print_char(b);
                            col += 1;
                        }
                        bi += 1;
                    }
                    print_char(b'\n');
                }
                row += 1;
            }
        }

        // --- Render status bar on row 24 (black on grey, attribute 0x70) ---
        let status_attr: u8 = 0x70;

        // Clear entire status row
        for col in 0..80 {
            vga_write(col, 24, b' ', status_attr);
        }

        // Write filename
        let mut pos: usize = 1;
        let mut fi: usize = 0;
        while fi < display_name.len() && pos < 40 {
            vga_write(pos, 24, display_name[fi], status_attr);
            pos += 1;
            fi += 1;
        }

        // Write percentage or END indicator
        pos += 1;
        let at_end = unsafe { scroll + page_lines >= LESS_LINE_COUNT };
        let at_start = scroll == 0;

        let indicator: &[u8] = if at_start && at_end {
            b"(ALL)"
        } else if at_end {
            b"(END)"
        } else if at_start {
            b"(TOP)"
        } else {
            // Show approximate percentage
            let pct = unsafe {
                if LESS_LINE_COUNT > 0 {
                    ((scroll + page_lines) * 100) / LESS_LINE_COUNT
                } else {
                    100
                }
            };
            // We'll format this inline below
            b""
        };

        if !indicator.is_empty() {
            let mut ii = 0;
            while ii < indicator.len() && pos < 60 {
                vga_write(pos, 24, indicator[ii], status_attr);
                pos += 1;
                ii += 1;
            }
        } else {
            // Render numeric percentage
            let pct = unsafe {
                if LESS_LINE_COUNT > 0 {
                    ((scroll + page_lines) * 100) / LESS_LINE_COUNT
                } else {
                    100
                }
            };
            // Format pct as up to 3 digits
            let d2 = (pct / 100) as u8;
            let d1 = ((pct / 10) % 10) as u8;
            let d0 = (pct % 10) as u8;
            if d2 > 0 {
                vga_write(pos, 24, b'0' + d2, status_attr);
                pos += 1;
            }
            if d2 > 0 || d1 > 0 {
                vga_write(pos, 24, b'0' + d1, status_attr);
                pos += 1;
            }
            vga_write(pos, 24, b'0' + d0, status_attr);
            pos += 1;
            vga_write(pos, 24, b'%', status_attr);
        }

        // Help hint on right side
        let hint = b"q:Quit j/k:Scroll Space:Page";
        let hint_start = 80 - hint.len() - 1;
        let mut hi = 0;
        while hi < hint.len() {
            vga_write(hint_start + hi, 24, hint[hi], status_attr);
            hi += 1;
        }

        // Position cursor at end of status bar (hide it)
        unsafe { set_cursor_pos(79, 24); }

        // --- Handle input ---
        let key = unsafe { get_char() };

        if key == b'q' || key == b'Q' {
            break;
        }

        let max_scroll = unsafe {
            if LESS_LINE_COUNT > page_lines {
                LESS_LINE_COUNT - page_lines
            } else {
                0
            }
        };

        if key == b'j' || key == KEY_DOWN || key == b'\n' || key == b'\r' {
            if scroll < max_scroll {
                scroll += 1;
            }
        } else if key == b'k' || key == KEY_UP {
            if scroll > 0 {
                scroll -= 1;
            }
        } else if key == b' ' || key == KEY_PAGEDOWN || key == b'f' {
            scroll += page_lines;
            if scroll > max_scroll {
                scroll = max_scroll;
            }
        } else if key == b'b' || key == KEY_PAGEUP {
            if scroll >= page_lines {
                scroll -= page_lines;
            } else {
                scroll = 0;
            }
        } else if key == b'g' || key == KEY_HOME {
            scroll = 0;
        } else if key == b'G' || key == KEY_END {
            scroll = max_scroll;
        } else if key == b'd' {
            // Half-page down
            scroll += page_lines / 2;
            if scroll > max_scroll {
                scroll = max_scroll;
            }
        } else if key == b'u' {
            // Half-page up
            if scroll >= page_lines / 2 {
                scroll -= page_lines / 2;
            } else {
                scroll = 0;
            }
        }
    }

    unsafe { clear_screen(); }
}

// =========================================================================
// Built-in Command: cat (Concatenate and print files / standard input)
// =========================================================================

fn cmd_cat(args: &[u8]) {
    let trimmed = trim_slice(args);

    if trimmed.is_empty() {
        // Read from pipe if active, otherwise interactive stdin until Ctrl+C or Ctrl+D (EOF / 0x04)
        unsafe {
            let bytes_read = syscall_pipe_read(SHELL_FILE_BUF.as_mut_ptr(), 32768);
            if bytes_read > 0 {
                let total = bytes_read as usize;
                for i in 0..total {
                    print_char(SHELL_FILE_BUF[i]);
                }
                return;
            }

            // Interactive mode: read characters from keyboard and echo them
            loop {
                let c = get_char();
                if c == 0x03 || c == 0x04 { // Ctrl+C or Ctrl+D (EOF)
                    if c == 0x03 {
                        puts(b"^C\n");
                    }
                    break;
                }
                print_char(c);
            }
        }
        return;
    }

    // Split args into multiple filenames separated by space
    let mut offset = 0;
    while offset < trimmed.len() {
        while offset < trimmed.len() && (trimmed[offset] == b' ' || trimmed[offset] == b'\t') {
            offset += 1;
        }
        if offset >= trimmed.len() {
            break;
        }

        let start = offset;
        while offset < trimmed.len() && trimmed[offset] != b' ' && trimmed[offset] != b'\t' {
            offset += 1;
        }
        let file_arg = &trimmed[start..offset];

        if bytes_eq(file_arg, b"-") {
            // Read from standard input / pipe
            unsafe {
                let bytes_read = syscall_pipe_read(SHELL_FILE_BUF.as_mut_ptr(), 32768);
                if bytes_read > 0 {
                    let total = bytes_read as usize;
                    for i in 0..total {
                        print_char(SHELL_FILE_BUF[i]);
                    }
                } else {
                    loop {
                        let c = get_char();
                        if c == 0x03 || c == 0x04 {
                            if c == 0x03 { puts(b"^C\n"); }
                            break;
                        }
                        print_char(c);
                    }
                }
            }
            continue;
        }

        let mut fname_buf = [0u8; 64];
        let len = if file_arg.len() < 63 { file_arg.len() } else { 63 };
        for i in 0..len {
            fname_buf[i] = file_arg[i];
        }
        fname_buf[len] = 0;

        unsafe {
            let bytes_read = vfs_read_file_by_name(fname_buf.as_ptr(), SHELL_FILE_BUF.as_mut_ptr(), 32768);
            if bytes_read < 0 {
                puts(b"cat: ");
                puts(file_arg);
                puts(b": No such file or directory\n");
            } else {
                let total = bytes_read as usize;
                for i in 0..total {
                    print_char(SHELL_FILE_BUF[i]);
                }
            }
        }
    }
}

// Built-in Command: help
fn cmd_help(_args: &[u8]) {
    puts(b"\nAvailable CykShell Commands:\n");
    for cmd in COMMANDS {
        if cmd.description.is_empty() {
            continue; // Skip uppercase aliases
        }
        puts(b"  ");
        puts(cmd.name);
        let mut pad = cmd.name.len();
        while pad < 12 {
            unsafe { print_char(b' '); }
            pad += 1;
        }
        puts(b"- ");
        puts(cmd.description);
        puts(b"\n");
    }

    // Also list external user commands from USERCMDS.TXT
    unsafe { list_user_commands(); }

    puts(b"\n");
}

fn cmd_run(args: &[u8]) {
    let filename = trim_slice(args);
    if filename.is_empty() {
        puts(b"Usage: run <script.SHE>\n");
        return;
    }
    run_script(filename);
}

// Configurable Command Registry Table
// Space is reserved below for future built-ins and user applications!
pub static COMMANDS: &[Command] = &[
    Command {
        name: b"run",
        description: b"Run a shell script (e.g. run startup.SHE)",
        handler: cmd_run,
    },
    Command {
        name: b"RUN",
        description: b"",
        handler: cmd_run,
    },
    Command {
        name: b"help",
        description: b"Display this list of recognized commands",
        handler: cmd_help,
    },
    Command {
        name: b"HELP",
        description: b"",
        handler: cmd_help,
    },
    Command {
        name: b"clear",
        description: b"Clear the terminal display",
        handler: cmd_clear,
    },
    Command {
        name: b"CLEAR",
        description: b"",
        handler: cmd_clear,
    },
    Command {
        name: b"dir",
        description: b"List files and directories in current volume",
        handler: cmd_dir,
    },
    Command {
        name: b"DIR",
        description: b"",
        handler: cmd_dir,
    },
    Command {
        name: b"rm",
        description: b"Delete a file from the disk (e.g. rm file.txt)",
        handler: cmd_rm,
    },
    Command {
        name: b"RM",
        description: b"",
        handler: cmd_rm,
    },
    Command {
        name: b"del",
        description: b"Delete a file (alias for rm)",
        handler: cmd_rm,
    },
    Command {
        name: b"DEL",
        description: b"",
        handler: cmd_rm,
    },
    Command {
        name: b"touch",
        description: b"Create an empty file or touch file",
        handler: cmd_touch,
    },
    Command {
        name: b"TOUCH",
        description: b"",
        handler: cmd_touch,
    },
    Command {
        name: b"cp",
        description: b"Copy a file (e.g. cp src.txt dst.txt)",
        handler: cmd_cp,
    },
    Command {
        name: b"CP",
        description: b"",
        handler: cmd_cp,
    },
    Command {
        name: b"wc",
        description: b"Count lines, words, and bytes in a file",
        handler: cmd_wc,
    },
    Command {
        name: b"WC",
        description: b"",
        handler: cmd_wc,
    },
    Command {
        name: b"hexdump",
        description: b"Display hex and ASCII dump of a file",
        handler: cmd_hexdump,
    },
    Command {
        name: b"HEXDUMP",
        description: b"",
        handler: cmd_hexdump,
    },
    Command {
        name: b"reboot",
        description: b"Reboot the operating system",
        handler: cmd_reboot,
    },
    Command {
        name: b"REBOOT",
        description: b"",
        handler: cmd_reboot,
    },
    Command {
        name: b"shutdown",
        description: b"Power off the virtual machine",
        handler: cmd_shutdown,
    },
    Command {
        name: b"SHUTDOWN",
        description: b"",
        handler: cmd_shutdown,
    },
    Command {
        name: b"exit",
        description: b"Exit the shell (shutdown)",
        handler: cmd_shutdown,
    },
    Command {
        name: b"EXIT",
        description: b"",
        handler: cmd_shutdown,
    },
    Command {
        name: b"mkdir",
        description: b"Create a directory (e.g. mkdir docs)",
        handler: cmd_mkdir,
    },
    Command {
        name: b"MKDIR",
        description: b"",
        handler: cmd_mkdir,
    },
    Command {
        name: b"rmdir",
        description: b"Remove an empty directory (e.g. rmdir docs)",
        handler: cmd_rmdir,
    },
    Command {
        name: b"RMDIR",
        description: b"",
        handler: cmd_rmdir,
    },
    Command {
        name: b"cd",
        description: b"Change current directory (e.g. cd docs, cd ..)",
        handler: cmd_cd,
    },
    Command {
        name: b"CD",
        description: b"",
        handler: cmd_cd,
    },
    Command {
        name: b"pwd",
        description: b"Print current working directory",
        handler: cmd_pwd,
    },
    Command {
        name: b"PWD",
        description: b"",
        handler: cmd_pwd,
    },
    Command {
        name: b"ls",
        description: b"List files (compact Linux-style listing)",
        handler: cmd_ls,
    },
    Command {
        name: b"LS",
        description: b"",
        handler: cmd_ls,
    },
    Command {
        name: b"less",
        description: b"View a file with scrolling (e.g. less file.txt)",
        handler: cmd_less,
    },
    Command {
        name: b"LESS",
        description: b"",
        handler: cmd_less,
    },
    Command {
        name: b"cat",
        description: b"Concatenate and print files (e.g. cat file.txt)",
        handler: cmd_cat,
    },
    Command {
        name: b"CAT",
        description: b"",
        handler: cmd_cat,
    },
    // Future user applications or additional built-ins can be registered here!
];

fn is_she_file(name: &[u8]) -> bool {
    let len = name.len();
    len >= 4 && name[len - 4] == b'.' &&
        (name[len - 3] == b'S' || name[len - 3] == b's') &&
        (name[len - 2] == b'H' || name[len - 2] == b'h') &&
        (name[len - 1] == b'E' || name[len - 1] == b'e')
}

fn run_script(filename: &[u8]) {
    if !is_she_file(filename) {
        puts(b"CykShell: scripts must use the .SHE extension\n");
        return;
    }
    unsafe {
        if SCRIPT_ACTIVE {
            puts(b"CykShell: nested scripts are not supported\n");
            return;
        }
        SCRIPT_ACTIVE = true;
    }

    if filename.len() >= 64 {
        puts(b"CykShell: script path is too long\n");
        unsafe { SCRIPT_ACTIVE = false; }
        return;
    }
    let mut filename_buf = [0u8; 64];
    for i in 0..filename.len() {
        filename_buf[i] = filename[i];
    }
    filename_buf[filename.len()] = 0;

    let bytes_read = unsafe {
        vfs_read_file_by_name(filename_buf.as_ptr(), SCRIPT_BUF.as_mut_ptr(), 32767)
    };
    if bytes_read < 0 {
        puts(b"CykShell: cannot open script: ");
        puts(filename);
        puts(b"\n");
        unsafe { SCRIPT_ACTIVE = false; }
        return;
    }

    let total = bytes_read as usize;
    unsafe { SCRIPT_BUF[total] = 0; }
    let mut line_start = 0;
    let mut index = 0;
    while index <= total {
        let at_end = index == total;
        let at_newline = !at_end && unsafe { SCRIPT_BUF[index] == b'\n' };
        if at_end || at_newline {
            let line = unsafe { trim_slice(&SCRIPT_BUF[line_start..index]) };
            if !line.is_empty() && line[0] != b'#' {
                if line.len() > 127 {
                    puts(b"CykShell: script line exceeds 127 bytes\n");
                } else {
                    dispatch_command(line);
                }
            }
            line_start = index + 1;
        }
        index += 1;
    }
    unsafe { SCRIPT_ACTIVE = false; }
}

// Command Dispatcher
fn dispatch_single_command(raw_line: &[u8]) -> bool {
    let trimmed = trim_slice(raw_line);
    if trimmed.is_empty() {
        return true;
    }

    // Split at first space into (command, args)
    let mut split_idx = 0;
    while split_idx < trimmed.len() && trimmed[split_idx] != b' ' && trimmed[split_idx] != b'\t' {
        split_idx += 1;
    }

    let mut cmd_name = &trimmed[..split_idx];
    let args = if split_idx < trimmed.len() {
        trim_slice(&trimmed[split_idx..])
    } else {
        b""
    };

    // Strip leading "./" or ".\\" path prefix if user typed e.g. "./edit.bin"
    if cmd_name.len() >= 2 && cmd_name[0] == b'.' && (cmd_name[1] == b'/' || cmd_name[1] == b'\\') {
        cmd_name = &cmd_name[2..];
    }

    if is_she_file(cmd_name) {
        run_script(cmd_name);
        return true;
    }

    // 1. Check built-in commands first
    for cmd in COMMANDS {
        if bytes_eq(cmd.name, cmd_name) {
            (cmd.handler)(args);
            return true;
        }
    }

    // 2. Check external user commands from USERCMDS.TXT
    //    We need null-terminated strings for the C function.
    unsafe {
        // Build null-terminated command name on the stack
        let mut cmd_buf = [0u8; 128];
        let cmd_len = if cmd_name.len() < 127 { cmd_name.len() } else { 127 };
        let mut i = 0;
        while i < cmd_len {
            cmd_buf[i] = cmd_name[i];
            i += 1;
        }
        cmd_buf[cmd_len] = 0;

        // Build null-terminated args string on the stack
        let mut args_buf = [0u8; 128];
        let args_len = if args.len() < 127 { args.len() } else { 127 };
        i = 0;
        while i < args_len {
            args_buf[i] = args[i];
            i += 1;
        }
        args_buf[args_len] = 0;

        if exec_user_command(cmd_buf.as_ptr(), args_buf.as_ptr()) != 0 {
            return true;
        }
    }

    puts(b"CykShell: command not found: ");
    puts(cmd_name);
    puts(b"\nType 'help' to view available commands.\n");
    false
}

const MAX_PIPE_STAGES: usize = 8;
const PIPE_BUFFER_CAPACITY: usize = 32768;

fn dispatch_command(raw_line: &[u8]) {
    let trimmed = trim_slice(raw_line);
    if trimmed.is_empty() {
        return;
    }

    let mut stages = [(0usize, 0usize); MAX_PIPE_STAGES];
    let mut stage_count = 0;
    let mut stage_start = 0;
    let mut quote = 0u8;
    let mut index = 0;
    while index <= trimmed.len() {
        if index == trimmed.len() {
            if quote != 0 {
                puts(b"CykShell: unterminated quote in pipeline\n");
                return;
            }
            if stage_count == MAX_PIPE_STAGES {
                puts(b"CykShell: pipeline supports at most 8 commands\n");
                return;
            }
            stages[stage_count] = (stage_start, index);
            stage_count += 1;
            break;
        }

        let byte = trimmed[index];
        if quote != 0 {
            if byte == quote {
                quote = 0;
            }
        } else if byte == b'\'' || byte == b'"' {
            quote = byte;
        } else if byte == b'|' {
            if stage_count == MAX_PIPE_STAGES - 1 {
                puts(b"CykShell: pipeline supports at most 8 commands\n");
                return;
            }
            stages[stage_count] = (stage_start, index);
            stage_count += 1;
            stage_start = index + 1;
        }
        index += 1;
    }

    if stage_count == 1 {
        dispatch_single_command(trimmed);
        return;
    }

    for &(start, end) in &stages[..stage_count] {
        if trim_slice(&trimmed[start..end]).is_empty() {
            puts(b"CykShell: empty command in pipeline\n");
            return;
        }
    }

    let mut input_ptr = core::ptr::null();
    let mut input_len = 0u32;
    for stage_index in 0..stage_count {
        let (start, end) = stages[stage_index];
        let stage = trim_slice(&trimmed[start..end]);
        let output_ptr = if stage_index + 1 < stage_count {
            unsafe { PIPE_BUFFERS[stage_index % 2].as_mut_ptr() }
        } else {
            core::ptr::null_mut()
        };

        unsafe {
            syscall_pipe_begin(
                input_ptr,
                input_len,
                output_ptr,
                if output_ptr.is_null() { 0 } else { PIPE_BUFFER_CAPACITY as u32 },
            );
            PIPE_CAPTURE_ACTIVE = !output_ptr.is_null();
        }
        dispatch_single_command(stage);
        let output_len = unsafe {
            PIPE_CAPTURE_ACTIVE = false;
            syscall_pipe_end()
        };

        if stage_index + 1 < stage_count {
            input_ptr = unsafe { PIPE_BUFFERS[stage_index % 2].as_ptr() };
            input_len = output_len;
        }
    }
}

// =========================================================================
// Main Interactive Shell Loop (Level 3 Entry Point)
// =========================================================================

// Static line buffer in .bss (avoids runtime stack allocations/zeroing)
static mut LINE_BUF: [u8; 128] = [0; 128];

// Command History (Up to 16 commands)
const MAX_HISTORY: usize = 16;
static mut HISTORY: [[u8; 128]; MAX_HISTORY] = [[0; 128]; MAX_HISTORY];
static mut HISTORY_LENS: [usize; MAX_HISTORY] = [0; MAX_HISTORY];
static mut HISTORY_COUNT: usize = 0;

// Autocomplete list
const AUTOCOMPLETE_LIST: &[&[u8]] = &[
    b"help",
    b"clear",
    b"dir",
    b"rm",
    b"del",
    b"reboot",
    b"shutdown",
    b"exit",
    b"edit",
    b"vi",
    b"hello",
    b"sysdemo",
    b"touch",
    b"cp",
    b"wc",
    b"hexdump",
    b"mkdir",
    b"rmdir",
    b"cd",
    b"pwd",
    b"ls",
    b"less",
    b"cat",
    b"soundtest",
    b"gfxdemo",
];

#[no_mangle]
pub extern "C" fn shell_main() -> ! {
    puts(b"\n========================================\n");
    puts_color(b" CykOS Interactive Shell (CykShell v0.2)\n", 0x0A); // VGA Green
    puts(b" Type 'help' for available commands.\n");
    puts(b"========================================\n\n");

    loop {
        let mut cwd_buf = [0u8; 128];
        let cwd_len = unsafe { vfs_getcwd(cwd_buf.as_mut_ptr(), 128) };
        if cwd_len > 0 {
            let ulen = cwd_len as usize;
            for i in 0..ulen {
                unsafe { print_char(cwd_buf[i]); }
            }
        } else {
            puts(b"/");
        }
        puts(b"] # ");

        let mut start_x: i32 = 0;
        let mut start_y: i32 = 0;
        unsafe { get_cursor_pos(&mut start_x, &mut start_y); }

        let mut line_len: usize = 0;
        let mut cursor_pos: usize = 0;
        let mut history_browse = unsafe { HISTORY_COUNT };
        let mut saved_scratch = [0u8; 128];
        let mut saved_scratch_len = 0;

        loop {
            let c = unsafe { get_char() };

            if c == 0x03 { // Ctrl+C
                puts(b"^C\n");
                line_len = 0;
                cursor_pos = 0;
                break;
            } else if c == b'\n' || c == b'\r' {
                unsafe { print_char(b'\n'); }
                break;
            } else if c == KEY_LEFT {
                if cursor_pos > 0 {
                    cursor_pos -= 1;
                    unsafe { set_cursor_pos(start_x + cursor_pos as i32, start_y); }
                }
            } else if c == KEY_RIGHT {
                if cursor_pos < line_len {
                    cursor_pos += 1;
                    unsafe { set_cursor_pos(start_x + cursor_pos as i32, start_y); }
                }
            } else if c == KEY_HOME {
                cursor_pos = 0;
                unsafe { set_cursor_pos(start_x, start_y); }
            } else if c == KEY_END {
                cursor_pos = line_len;
                unsafe { set_cursor_pos(start_x + line_len as i32, start_y); }
            } else if c == KEY_UP {
                // Browse back in history
                unsafe {
                    if HISTORY_COUNT > 0 && history_browse > 0 {
                        let available = if HISTORY_COUNT > MAX_HISTORY { MAX_HISTORY } else { HISTORY_COUNT };
                        let min_allowed = HISTORY_COUNT - available;
                        if history_browse > min_allowed {
                            if history_browse == HISTORY_COUNT {
                                // Save current typed line
                                for i in 0..line_len { saved_scratch[i] = LINE_BUF[i]; }
                                saved_scratch_len = line_len;
                            }
                            history_browse -= 1;
                            let slot = history_browse % MAX_HISTORY;
                            let new_len = HISTORY_LENS[slot];
                            for i in 0..new_len { LINE_BUF[i] = HISTORY[slot][i]; }

                            // Redraw line
                            set_cursor_pos(start_x, start_y);
                            for i in 0..new_len { print_char(LINE_BUF[i]); }
                            let mut cur = new_len;
                            while cur < line_len { print_char(b' '); cur += 1; }
                            line_len = new_len;
                            cursor_pos = new_len;
                            set_cursor_pos(start_x + cursor_pos as i32, start_y);
                        }
                    }
                }
            } else if c == KEY_DOWN {
                // Browse forward in history
                unsafe {
                    if history_browse < HISTORY_COUNT {
                        history_browse += 1;
                        let new_len = if history_browse == HISTORY_COUNT {
                            for i in 0..saved_scratch_len { LINE_BUF[i] = saved_scratch[i]; }
                            saved_scratch_len
                        } else {
                            let slot = history_browse % MAX_HISTORY;
                            let hlen = HISTORY_LENS[slot];
                            for i in 0..hlen { LINE_BUF[i] = HISTORY[slot][i]; }
                            hlen
                        };

                        // Redraw line
                        set_cursor_pos(start_x, start_y);
                        for i in 0..new_len { print_char(LINE_BUF[i]); }
                        let mut cur = new_len;
                        while cur < line_len { print_char(b' '); cur += 1; }
                        line_len = new_len;
                        cursor_pos = new_len;
                        set_cursor_pos(start_x + cursor_pos as i32, start_y);
                    }
                }
            } else if c == b'\t' {
                // Tab Autocompletion
                if line_len > 0 && cursor_pos == line_len {
                    unsafe {
                        let prefix = &LINE_BUF[..line_len];
                        let mut match_candidate: Option<&[u8]> = None;
                        let mut match_count = 0;

                        for &cand in AUTOCOMPLETE_LIST {
                            if cand.len() >= prefix.len() && bytes_eq(&cand[..prefix.len()], prefix) {
                                match_candidate = Some(cand);
                                match_count += 1;
                            }
                        }

                        if match_count == 1 {
                            if let Some(cand) = match_candidate {
                                for i in 0..cand.len() { LINE_BUF[i] = cand[i]; }
                                LINE_BUF[cand.len()] = b' ';
                                let new_len = cand.len() + 1;

                                set_cursor_pos(start_x, start_y);
                                for i in 0..new_len { print_char(LINE_BUF[i]); }
                                line_len = new_len;
                                cursor_pos = new_len;
                                set_cursor_pos(start_x + cursor_pos as i32, start_y);
                            }
                        }
                    }
                }
            } else if c == 0x08 { // Backspace
                unsafe {
                    if cursor_pos > 0 {
                        let mut i = cursor_pos - 1;
                        while i + 1 < line_len {
                            LINE_BUF[i] = LINE_BUF[i + 1];
                            i += 1;
                        }
                        line_len -= 1;
                        cursor_pos -= 1;

                        set_cursor_pos(start_x, start_y);
                        for j in 0..line_len { print_char(LINE_BUF[j]); }
                        print_char(b' ');
                        set_cursor_pos(start_x + cursor_pos as i32, start_y);
                    }
                }
            } else if c == KEY_DELETE { // Delete
                unsafe {
                    if cursor_pos < line_len {
                        let mut i = cursor_pos;
                        while i + 1 < line_len {
                            LINE_BUF[i] = LINE_BUF[i + 1];
                            i += 1;
                        }
                        line_len -= 1;

                        set_cursor_pos(start_x, start_y);
                        for j in 0..line_len { print_char(LINE_BUF[j]); }
                        print_char(b' ');
                        set_cursor_pos(start_x + cursor_pos as i32, start_y);
                    }
                }
            } else if c >= 32 && c <= 126 { // Printable ASCII range
                unsafe {
                    if line_len < LINE_BUF.len() - 1 {
                        if cursor_pos == line_len {
                            LINE_BUF[line_len] = c;
                            line_len += 1;
                            cursor_pos += 1;
                            print_char(c);
                        } else {
                            let mut i = line_len;
                            while i > cursor_pos {
                                LINE_BUF[i] = LINE_BUF[i - 1];
                                i -= 1;
                            }
                            LINE_BUF[cursor_pos] = c;
                            line_len += 1;
                            cursor_pos += 1;

                            set_cursor_pos(start_x, start_y);
                            for j in 0..line_len { print_char(LINE_BUF[j]); }
                            set_cursor_pos(start_x + cursor_pos as i32, start_y);
                        }
                    }
                }
            }
        }

        // Save command to history (if non-empty and not identical to previous)
        if line_len > 0 {
            unsafe {
                let is_dup = HISTORY_COUNT > 0 &&
                    HISTORY_LENS[(HISTORY_COUNT - 1) % MAX_HISTORY] == line_len &&
                    bytes_eq(&HISTORY[(HISTORY_COUNT - 1) % MAX_HISTORY][..line_len], &LINE_BUF[..line_len]);

                if !is_dup {
                    let slot = HISTORY_COUNT % MAX_HISTORY;
                    for i in 0..line_len {
                        HISTORY[slot][i] = LINE_BUF[i];
                    }
                    HISTORY_LENS[slot] = line_len;
                    HISTORY_COUNT += 1;
                }
            }
        }

        unsafe {
            dispatch_command(&LINE_BUF[..line_len]);
        }
    }
}

#[no_mangle]
pub extern "C" fn main(_argc: isize, _argv: *const *const u8) -> i32 {
    shell_main()
}
