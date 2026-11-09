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
const KEY_DELETE: u8   = 0x89;

// Level 1 Kernel & Driver C Function Declarations
extern "C" {
    fn clear_screen();
    fn print_char(c: u8);
    fn print_string(s: *const u8);
    fn print_color_string(s: *const u8, color: u8);
    fn get_char() -> u8;
    fn has_key() -> i32;
    fn secwait(seconds: u32);
    fn outb(port: u16, data: u8);
    fn outw(port: u16, data: u16);
    fn get_cursor_pos(x: *mut i32, y: *mut i32);
    fn set_cursor_pos(x: i32, y: i32);
    fn fat16_list_directory(callback: extern "C" fn(*const u8, u32, u8)) -> i32;
    fn fat16_delete_file(filename: *const u8) -> i32;
    fn fat16_read_file_by_name(filename: *const u8, buffer: *mut u8, max_bytes: u32) -> i32;
    fn fat16_write_file(filename: *const u8, data: *const u8, size: u32) -> i32;
    fn fat16_mkdir(path: *const u8) -> i32;
    fn fat16_rmdir(path: *const u8) -> i32;
    fn fat16_chdir(path: *const u8) -> i32;
    fn fat16_getcwd(buffer: *mut u8, max_bytes: u32) -> i32;

    // External user command support (usercmd.c)
    fn exec_user_command(cmd_name: *const u8, args: *const u8) -> i32;
    fn list_user_commands();
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
    puts(b"\n Volume in drive C is CYKOS_DISK\n");
    puts(b" Directory of C:");
    let mut buf = [0u8; 128];
    let len = unsafe { fat16_getcwd(buf.as_mut_ptr(), 128) };
    if len > 0 {
        let ulen = len as usize;
        for i in 0..ulen {
            unsafe { print_char(buf[i]); }
        }
    } else {
        puts(b"\\");
    }
    puts(b"\n\n");

    unsafe {
        DIR_FILE_COUNT = 0;
        fat16_list_directory(dir_entry_callback);
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

    let res = unsafe { fat16_delete_file(fname_buf.as_ptr()) };
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
        let exists = fat16_read_file_by_name(fname_buf.as_ptr(), check_buf.as_mut_ptr(), 1);
        if exists < 0 {
            let dummy = 0u8;
            let res = fat16_write_file(fname_buf.as_ptr(), &dummy, 0);
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
        let bytes_read = fat16_read_file_by_name(src_buf.as_ptr(), SHELL_FILE_BUF.as_mut_ptr(), 32768);
        if bytes_read < 0 {
            puts(b"cp: cannot open source file: ");
            puts(src);
            puts(b"\n");
            return;
        }

        let res = fat16_write_file(dst_buf.as_ptr(), SHELL_FILE_BUF.as_ptr(), bytes_read as u32);
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
    if filename.is_empty() {
        puts(b"Usage: wc <filename>\n");
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
        let bytes_read = fat16_read_file_by_name(fname_buf.as_ptr(), SHELL_FILE_BUF.as_mut_ptr(), 32768);
        if bytes_read < 0 {
            puts(b"wc: cannot open file: ");
            puts(filename);
            puts(b"\n");
            return;
        }

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
        let bytes_read = fat16_read_file_by_name(fname_buf.as_ptr(), SHELL_FILE_BUF.as_mut_ptr(), 16384);
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

    let res = unsafe { fat16_mkdir(dname_buf.as_ptr()) };
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

    let res = unsafe { fat16_rmdir(dname_buf.as_ptr()) };
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
            let root = [b'\\', 0];
            fat16_chdir(root.as_ptr())
        } else {
            fat16_chdir(path_buf.as_ptr())
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
    let len = unsafe { fat16_getcwd(buf.as_mut_ptr(), 128) };
    if len >= 0 {
        puts(b"C:");
        let ulen = len as usize;
        for i in 0..ulen {
            unsafe { print_char(buf[i]); }
        }
        puts(b"\n");
    } else {
        puts(b"pwd: error getting current directory\n");
    }
}

// Built-in Command: help
fn cmd_help(_args: &[u8]) {
    puts(b"\nAvailable CykShell Commands:\n");
    for cmd in COMMANDS {
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

// Configurable Command Registry Table
// Space is reserved below for future built-ins and user applications!
pub static COMMANDS: &[Command] = &[
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
    // Future user applications or additional built-ins can be registered here!
];

// Command Dispatcher
fn dispatch_command(raw_line: &[u8]) {
    let trimmed = trim_slice(raw_line);
    if trimmed.is_empty() {
        return;
    }

    // Split at first space into (command, args)
    let mut split_idx = 0;
    while split_idx < trimmed.len() && trimmed[split_idx] != b' ' {
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

    // 1. Check built-in commands first
    for cmd in COMMANDS {
        if bytes_eq(cmd.name, cmd_name) {
            (cmd.handler)(args);
            return;
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
            return;
        }
    }

    puts(b"CykShell: command not found: ");
    puts(cmd_name);
    puts(b"\nType 'help' to view available commands.\n");
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
];

#[no_mangle]
pub extern "C" fn shell_main() -> ! {
    puts(b"\n========================================\n");
    puts_color(b" CykOS Interactive Shell (CykShell v0.2)\n", 0x0A); // VGA Green
    puts(b" Type 'help' for available commands.\n");
    puts(b"========================================\n\n");

    loop {
        let mut cwd_buf = [0u8; 128];
        let cwd_len = unsafe { fat16_getcwd(cwd_buf.as_mut_ptr(), 128) };
        puts(b"C:");
        if cwd_len > 0 {
            let ulen = cwd_len as usize;
            for i in 0..ulen {
                unsafe { print_char(cwd_buf[i]); }
            }
        } else {
            puts(b"\\");
        }
        puts(b"> ");

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

            if c == b'\n' || c == b'\r' {
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
