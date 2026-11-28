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
const SYS_WAIT: u32 = 7;

// Level 1 Kernel & Driver C Function Declarations
extern "C" {
    fn clear_screen();
    fn console_clear();
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
    fn vfs_find_file(path: *const u8, output: *mut VfsFile) -> i32;
    fn vfs_delete_file(filename: *const u8) -> i32;
    fn vfs_read_file_by_name(filename: *const u8, buffer: *mut u8, max_bytes: u32) -> i32;
    fn vfs_write_file(filename: *const u8, data: *const u8, size: u32) -> i32;
    fn vfs_mkdir(path: *const u8) -> i32;
    fn vfs_rmdir(path: *const u8) -> i32;
    fn vfs_chdir(path: *const u8) -> i32;
    fn vfs_getcwd(buffer: *mut u8, max_bytes: u32) -> i32;
    fn permissions_uid() -> u32;
    fn permissions_gid() -> u32;
    fn permissions_umask() -> u32;
    fn permissions_set_umask(mask: u32) -> u32;
    fn permissions_get(path: *const u8, mode: *mut u32, uid: *mut u32, gid: *mut u32) -> i32;
    fn permissions_check(path: *const u8, requested: u32) -> i32;
    fn permissions_chmod(path: *const u8, mode: u32) -> i32;
    fn permissions_chown(path: *const u8, uid: u32, gid: u32) -> i32;
    fn permissions_set_uid(uid: u32) -> i32;
    fn permissions_set_gid(gid: u32) -> i32;
    fn permissions_set_identity(uid: u32, gid: u32);
    fn accounts_count() -> u32;
    fn accounts_get(index: u32, name: *mut u8, uid: *mut u32, gid: *mut u32, flags: *mut u32) -> i32;
    fn accounts_lookup(name: *const u8, uid: *mut u32, gid: *mut u32, flags: *mut u32) -> i32;
    fn accounts_create(name: *const u8, uid: u32, gid: u32, flags: u32) -> i32;
    fn accounts_delete(name: *const u8) -> i32;
    fn accounts_set_flags(name: *const u8, flags: u32) -> i32;
    fn accounts_set_password(name: *const u8, password: *const u8) -> i32;
    fn accounts_has_password(name: *const u8) -> i32;
    fn accounts_authenticate(name: *const u8, password: *const u8) -> i32;

    // External user command support (usercmd.c)
    fn exec_user_command(cmd_name: *const u8, args: *const u8) -> i32;
    fn list_user_commands();

    // Temporary in-memory standard input/output for synchronous pipelines.
    fn syscall_pipe_begin(input: *const u8, input_len: u32, output: *mut u8, output_capacity: u32);
    fn syscall_pipe_end() -> u32;
    fn syscall_pipe_write(data: *const u8, length: u32) -> u32;
    fn syscall_pipe_read(data: *mut u8, length: u32) -> i32;
    fn sched_list_tasks(out: *mut SchedTaskInfo, max_tasks: i32) -> i32;
    fn net_get_info(out: *mut NetInfo) -> i32;
    fn net_set_config(ip: u32, netmask: u32, gateway: u32) -> i32;
    fn net_dhcp() -> i32;
    fn net_ping(dst_ip: u32, seq: u16, timeout_ms: u32, rtt_ms_out: *mut u32) -> i32;
}

#[repr(C)]
#[derive(Copy, Clone)]
struct NetInfo {
    available: i32,
    link_up: i32,
    mac: [u8; 6],
    ip: u32,
    netmask: u32,
    gateway: u32,
    dns: u32,
    rx_packets: u32,
    tx_packets: u32,
    hw_name: [u8; 32],
}

#[repr(C)]
#[derive(Copy, Clone)]
struct SchedTaskInfo {
    pid: u32,
    state: u32,
    name: [u8; 24],
    total_ticks: u32,
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
        console_clear();
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
        secwait(3);

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
static mut REDIRECT_PATH: [u8; 64] = [0; 64];
static mut REDIRECT_ACTIVE: bool = false;
static mut REDIRECT_APPEND: bool = false;

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
        let mut existing = core::mem::MaybeUninit::<VfsFile>::uninit();
        if vfs_find_file(fname_buf.as_ptr(), existing.as_mut_ptr()) != 0 {
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

    if path == b"~" {
        let uid = unsafe { permissions_uid() };
        let mut name = [0u8; 24];
        if username_for_uid(uid, &mut name) {
            let name_len = name.iter().position(|&b| b == 0).unwrap_or(24);
            if name_len + 6 < 63 {
                path_buf[..6].copy_from_slice(b"/HOME/");
                for i in 0..name_len {
                    let c = name[i];
                    path_buf[6 + i] = if c >= b'a' && c <= b'z' { c - 32 } else { c };
                }
                path_buf[6 + name_len] = 0;
                let mut probe = core::mem::MaybeUninit::<VfsFile>::uninit();
                if unsafe { vfs_find_file(path_buf.as_ptr(), probe.as_mut_ptr()) } == 0 {
                    if unsafe { vfs_chdir(path_buf.as_ptr()) } == 0 {
                        return;
                    }
                }
            }
        }
        let root = [b'/', 0];
        let _ = unsafe { vfs_chdir(root.as_ptr()) };
        return;
    }

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

fn cmd_echo(args: &[u8]) {
    puts(args);
    print_char(b'\n');
}

fn split_word(input: &[u8]) -> (&[u8], &[u8]) {
    let mut end = 0;
    while end < input.len() && input[end] != b' ' && input[end] != b'\t' { end += 1; }
    (&input[..end], trim_slice(&input[end..]))
}

fn parse_number(input: &[u8], radix: u32) -> Option<u32> {
    if input.is_empty() { return None; }
    let mut value = 0u32;
    for &byte in input {
        let digit = match byte {
            b'0'..=b'9' => (byte - b'0') as u32,
            b'a'..=b'f' => (byte - b'a' + 10) as u32,
            b'A'..=b'F' => (byte - b'A' + 10) as u32,
            _ => return None,
        };
        if digit >= radix || value > (u32::MAX - digit) / radix { return None; }
        value = value * radix + digit;
    }
    Some(value)
}

fn print_octal_mode(mode: u32) {
    for shift in [9, 6, 3, 0] {
        print_char(b'0' + ((mode >> shift) & 7) as u8);
    }
}

fn cmd_id(_args: &[u8]) {
    unsafe {
        let uid = permissions_uid();
        let gid = permissions_gid();
        puts(b"uid="); print_u32(uid);
        let mut name = [0u8; 24];
        if username_for_uid(uid, &mut name) {
            puts(b"(");
            puts(&name[..name.iter().position(|&b| b == 0).unwrap_or(24)]);
            puts(b")");
        }
        puts(b" gid="); print_u32(gid);
        print_char(b'\n');
    }
}

fn cmd_whoami(_args: &[u8]) {
    let uid = unsafe { permissions_uid() };
    let mut name = [0u8; 24];
    if username_for_uid(uid, &mut name) {
        puts(&name[..name.iter().position(|&b| b == 0).unwrap_or(24)]);
        print_char(b'\n');
    } else if uid == 0 {
        puts(b"root\n");
    } else {
        puts(b"uid-"); print_u32(uid); print_char(b'\n');
    }
}

fn cmd_chmod(args: &[u8]) {
    let (mode_text, rest) = split_word(trim_slice(args));
    let (path, extra) = split_word(rest);
    let mode = parse_number(mode_text, 8);
    if mode.is_none() || path.is_empty() || !extra.is_empty() || mode.unwrap() > 0o7777 {
        puts(b"Usage: chmod <octal-mode> <path> (for example chmod 0644 file.txt)\n");
        return;
    }
    let mut path_buf = [0u8; 128];
    if path.len() >= path_buf.len() { puts(b"chmod: path is too long\n"); return; }
    path_buf[..path.len()].copy_from_slice(path);
    unsafe {
        if permissions_chmod(path_buf.as_ptr(), mode.unwrap()) == 0 {
            puts(b"Mode set to "); print_octal_mode(mode.unwrap()); print_char(b'\n');
        } else { puts(b"chmod: permission denied or path not found\n"); }
    }
}

fn cmd_chown(args: &[u8]) {
    let (owner_text, rest) = split_word(trim_slice(args));
    let (path, extra) = split_word(rest);
    if owner_text.is_empty() || path.is_empty() || !extra.is_empty() {
        puts(b"Usage: chown <uid[:gid]> <path>\n");
        return;
    }
    let colon = owner_text.iter().position(|&b| b == b':');
    let uid_text = colon.map_or(owner_text, |i| &owner_text[..i]);
    let gid_text = colon.map(|i| &owner_text[i + 1..]);
    let uid = match parse_number(uid_text, 10) { Some(v) => v, None => { puts(b"chown: invalid uid/gid\n"); return; } };
    let mut path_buf = [0u8; 128];
    if path.len() >= path_buf.len() { puts(b"chown: path is too long\n"); return; }
    path_buf[..path.len()].copy_from_slice(path);
    unsafe {
        let mut mode = 0u32;
        let mut old_uid = 0u32;
        let mut old_gid = 0u32;
        if permissions_get(path_buf.as_ptr(), &mut mode, &mut old_uid, &mut old_gid) != 0 {
            puts(b"chown: path not found\n"); return;
        }
        let gid = match gid_text {
            Some(text) => match parse_number(text, 10) {
                Some(v) => v,
                None => { puts(b"chown: invalid uid/gid\n"); return; }
            },
            None => old_gid,
        };
        if permissions_chown(path_buf.as_ptr(), uid, gid) == 0 {
            puts(b"Ownership updated\n");
        } else { puts(b"chown: root privileges required\n"); }
    }
}

fn cmd_umask(args: &[u8]) {
    let text = trim_slice(args);
    if text.is_empty() {
        let mask = unsafe { permissions_umask() };
        print_octal_mode(mask);
        print_char(b'\n');
        return;
    }
    let (word, rest) = split_word(text);
    let parsed = parse_number(word, 8);
    if parsed.is_none() || parsed.unwrap() > 0o777 || !rest.is_empty() {
        puts(b"Usage: umask [octal-mask]\n");
        return;
    }
    let previous = unsafe { permissions_set_umask(parsed.unwrap()) };
    puts(b"umask "); print_octal_mode(previous); puts(b" -> ");
    print_octal_mode(parsed.unwrap()); print_char(b'\n');
}

fn cmd_stat(args: &[u8]) {
    let path = trim_slice(args);
    if path.is_empty() || path.iter().any(|&b| b == b' ' || b == b'\t') {
        puts(b"Usage: stat <path>\n"); return;
    }
    let mut path_buf = [0u8; 128];
    if path.len() >= path_buf.len() { puts(b"stat: path is too long\n"); return; }
    path_buf[..path.len()].copy_from_slice(path);
    unsafe {
        let mut mode = 0u32; let mut uid = 0u32; let mut gid = 0u32;
        if permissions_check(path_buf.as_ptr(), 0) != 0 ||
            permissions_get(path_buf.as_ptr(), &mut mode, &mut uid, &mut gid) != 0 {
            puts(b"stat: path not found or inaccessible\n"); return;
        }
        let mut file = core::mem::MaybeUninit::<VfsFile>::uninit();
        if vfs_find_file(path_buf.as_ptr(), file.as_mut_ptr()) != 0 {
            puts(b"stat: path not found\n"); return;
        }
        let file = file.assume_init();
        if mode & 0o170000 == 0o040000 { puts(b"directory "); }
        else { puts(b"file "); }
        puts(path); puts(b" mode="); print_octal_mode(mode & 0o7777);
        puts(b" uid="); print_u32(uid); puts(b" gid="); print_u32(gid);
        puts(b" size="); print_u32(file.size); puts(b" bytes\n");
    }
}

#[repr(C)]
struct VfsFile {
    name: [u8; 13],
    size: u32,
    first_cluster: u32,
    attributes: u8,
    volume_index: u8,
    driver: *const u8,
}

fn cmd_access(args: &[u8]) {
    let (mode_text, path) = split_word(trim_slice(args));
    if mode_text.is_empty() || path.is_empty() || path.iter().any(|&b| b == b' ' || b == b'\t') {
        puts(b"Usage: access <rwx> <path>\n"); return;
    }
    let mut requested = 0u32;
    for &byte in mode_text {
        match byte {
            b'r' => requested |= 4,
            b'w' => requested |= 2,
            b'x' => requested |= 1,
            _ => { puts(b"access: use only r, w, and x\n"); return; }
        }
    }
    let mut path_buf = [0u8; 128];
    if path.len() >= path_buf.len() { puts(b"access: path is too long\n"); return; }
    path_buf[..path.len()].copy_from_slice(path);
    if unsafe { permissions_check(path_buf.as_ptr(), requested) } == 0 {
        puts(b"allowed\n");
    } else { puts(b"denied\n"); }
}

fn cmd_runas(args: &[u8]) {
    let (uid_text, rest) = split_word(trim_slice(args));
    let (gid_text, command_and_args) = split_word(rest);
    let (command, app_args) = split_word(command_and_args);
    if uid_text.is_empty() || gid_text.is_empty() || command.is_empty() ||
        unsafe { permissions_uid() } != 0 {
        puts(b"Usage: runas <uid> <gid> <app> [arguments] (root only)\n"); return;
    }
    let uid = match parse_number(uid_text, 10) { Some(v) => v, None => { puts(b"runas: invalid uid/gid\n"); return; } };
    let gid = match parse_number(gid_text, 10) { Some(v) => v, None => { puts(b"runas: invalid uid/gid\n"); return; } };
    if command.len() >= 128 || app_args.len() >= 128 { puts(b"runas: command line is too long\n"); return; }
    unsafe {
        let old_uid = permissions_uid();
        let old_gid = permissions_gid();
        let old_umask = permissions_umask();
        if permissions_set_gid(gid) != 0 || permissions_set_uid(uid) != 0 {
            permissions_set_identity(old_uid, old_gid);
            puts(b"runas: unable to set identity\n"); return;
        }
        let mut command_buf = [0u8; 128];
        let mut args_buf = [0u8; 128];
        command_buf[..command.len()].copy_from_slice(command);
        args_buf[..app_args.len()].copy_from_slice(app_args);
        let _ = exec_user_command(command_buf.as_ptr(), args_buf.as_ptr());
        permissions_set_identity(old_uid, old_gid);
        let _ = permissions_set_umask(old_umask);
    }
}

fn read_secret(prompt: &[u8]) -> ([u8; 65], usize) {
    puts(prompt);
    let mut secret = [0u8; 65];
    let mut len = 0usize;
    loop {
        let key = unsafe { get_char() };
        if key == b'\r' || key == b'\n' { print_char(b'\n'); break; }
        if key == 8 || key == 127 {
            if len > 0 { len -= 1; secret[len] = 0; }
            continue;
        }
        if key >= 0x20 && key < 0x7f && len < 64 { secret[len] = key; len += 1; }
    }
    (secret, len)
}

fn username_for_uid(uid: u32, out: &mut [u8; 24]) -> bool {
    let mut i = 0;
    while i < unsafe { accounts_count() } {
        let mut account_uid = 0; let mut gid = 0; let mut flags = 0;
        if unsafe { accounts_get(i, out.as_mut_ptr(), &mut account_uid, &mut gid, &mut flags) } == 0 && account_uid == uid { return true; }
        i += 1;
    }
    false
}

fn cmd_users(_args: &[u8]) {
    let mut i = 0;
    while i < unsafe { accounts_count() } {
        let mut name = [0u8; 24]; let mut uid = 0; let mut gid = 0; let mut flags = 0;
        if unsafe { accounts_get(i, name.as_mut_ptr(), &mut uid, &mut gid, &mut flags) } == 0 {
            puts(&name[..name.iter().position(|&b| b == 0).unwrap_or(name.len())]);
            puts(b" uid="); print_u32(uid); puts(b" gid="); print_u32(gid);
            if flags & 1 != 0 { puts(b" sudo"); }
            print_char(b'\n');
        }
        i += 1;
    }
}

fn cmd_useradd(args: &[u8]) {
    let (name, rest) = split_word(trim_slice(args));
    let (uid_text, rest) = split_word(rest);
    let (gid_text, flags_text) = split_word(rest);
    if unsafe { permissions_uid() } != 0 || name.is_empty() || name.len() >= 24 || uid_text.is_empty() || gid_text.is_empty() ||
        (!flags_text.is_empty() && flags_text != b"sudo") {
        puts(b"Usage: useradd <name> <uid> <gid> [sudo] (root only)\n"); return;
    }
    let uid = match parse_number(uid_text,10) {Some(v)=>v,None=>{puts(b"useradd: invalid uid\n");return;}};
    let gid = match parse_number(gid_text,10) {Some(v)=>v,None=>{puts(b"useradd: invalid gid\n");return;}};
    let mut account_name=[0u8;24]; account_name[..name.len()].copy_from_slice(name);
    let flags=if flags_text==b"sudo" {1} else {0};
    if unsafe { accounts_create(account_name.as_ptr(),uid,gid,flags) } == 0 {
        puts(b"Account created; set its password with passwd ");puts(name);print_char(b'\n');
        // Create home directory /HOME/<NAME>
        let mut home = [0u8; 64];
        home[..6].copy_from_slice(b"/HOME/");
        for i in 0..name.len() {
            let c = name[i];
            home[6 + i] = if c >= b'a' && c <= b'z' { c - 32 } else { c };
        }
        home[6 + name.len()] = 0;
        unsafe {
            // Ensure /HOME exists first
            let home_root = b"/HOME\0";
            let _ = vfs_mkdir(home_root.as_ptr());
            if vfs_mkdir(home.as_ptr()) == 0 {
                let _ = permissions_chown(home.as_ptr(), uid, gid);
                let _ = permissions_chmod(home.as_ptr(), 0o0750);
                puts(b"Home directory created: ");
                puts(&home[..6 + name.len()]);
                print_char(b'\n');
            }
        }
    }
    else { puts(b"useradd: account or UID already exists, or database is unavailable\n"); }
}

fn cmd_userdel(args: &[u8]) {
    let name=trim_slice(args);
    if unsafe{permissions_uid()}!=0||name.is_empty()||name.len()>=24||name.iter().any(|&b|b==b' '||b==b'\t') {puts(b"Usage: userdel <name> (root only; root cannot be deleted)\n");return;}
    let mut account_name=[0u8;24];account_name[..name.len()].copy_from_slice(name);
    if unsafe{accounts_delete(account_name.as_ptr())}==0 {puts(b"Account deleted; files owned by its UID are unchanged\n");}
    else {puts(b"userdel: account not found, root account protected, or database unavailable\n");}
}

fn cmd_usermod(args: &[u8]) {
    let (name,setting)=split_word(trim_slice(args));
    if unsafe{permissions_uid()}!=0||name.is_empty()||name.len()>=24||(setting!=b"sudo"&&setting!=b"nosudo") {puts(b"Usage: usermod <name> sudo|nosudo (root only)\n");return;}
    let mut account_name=[0u8;24];account_name[..name.len()].copy_from_slice(name);
    let flags=if setting==b"sudo" {1} else {0};
    if unsafe{accounts_set_flags(account_name.as_ptr(),flags)}==0 {puts(b"Account privileges updated\n");}
    else {puts(b"usermod: account not found, root account protected, or database unavailable\n");}
}

fn cmd_passwd(args: &[u8]) {
    let target_arg=trim_slice(args);
    if target_arg.iter().any(|&b|b==b' '||b==b'\t') {puts(b"Usage: passwd [username]\n");return;}
    let current_uid=unsafe{permissions_uid()};
    let mut current=[0u8;24];
    if target_arg.is_empty() {
        if !username_for_uid(current_uid,&mut current){puts(b"passwd: current UID has no account\n");return;}
    } else {
        if current_uid!=0 {puts(b"passwd: root privileges required to set another user's password\n");return;}
        if target_arg.len()>=24 {puts(b"passwd: invalid username\n");return;}
        current[..target_arg.len()].copy_from_slice(target_arg);
    }
    let (first,first_len)=read_secret(b"New password: ");
    if first_len==0 {puts(b"passwd: empty passwords are not allowed\n");return;}
    let (second,second_len)=read_secret(b"Retype new password: ");
    if first_len!=second_len || first[..first_len]!=second[..second_len] {puts(b"passwd: passwords do not match\n");return;}
    let name=current.as_ptr();
    if unsafe{accounts_set_password(name,first.as_ptr())}==0 {puts(b"Password updated\n");} else {puts(b"passwd: unable to update password\n");}
}

fn cmd_su(args: &[u8]) {
    let target=trim_slice(args);
    if target.iter().any(|&b|b==b' '||b==b'\t') {puts(b"Usage: su [username]\n");return;}
    let mut target_name=[0u8;24];
    if target.is_empty() {target_name[..4].copy_from_slice(b"root");}
    else if target.len()<24 {target_name[..target.len()].copy_from_slice(target);}
    else {puts(b"su: invalid username\n");return;}
    let mut uid=0;let mut gid=0;let mut flags=0;
    if unsafe{accounts_lookup(target_name.as_ptr(),&mut uid,&mut gid,&mut flags)}!=0 {puts(b"su: unknown account\n");return;}
    if unsafe{permissions_uid()}!=0 {
        let (mut password,len)=read_secret(b"Password: ");
        password[len]=0;
        if unsafe{accounts_authenticate(target_name.as_ptr(),password.as_ptr())}!=0 {puts(b"su: authentication failed\n");return;}
    }
    unsafe{permissions_set_identity(uid,gid);}
    puts(b"Switched to ");puts(&target_name[..target_name.iter().position(|&b|b==0).unwrap_or(24)]);print_char(b'\n');
}

fn cmd_sudo(args: &[u8]) {
    let command=trim_slice(args);
    if command.is_empty(){puts(b"Usage: sudo <command> [arguments]\n");return;}
    let uid=unsafe{permissions_uid()};
    if uid!=0 {
        let mut name=[0u8;24];let mut gid=0;let mut flags=0;
        if !username_for_uid(uid,&mut name)||unsafe{accounts_lookup(name.as_ptr(),core::ptr::null_mut(),&mut gid,&mut flags)}!=0||flags&1==0 {
            puts(b"sudo: account is not authorized\n");return;
        }
        let (mut password,len)=read_secret(b"Password: ");password[len]=0;
        if unsafe{accounts_authenticate(name.as_ptr(),password.as_ptr())}!=0 {puts(b"sudo: authentication failed\n");return;}
    }
    let old_uid=unsafe{permissions_uid()};let old_gid=unsafe{permissions_gid()};
    unsafe{permissions_set_identity(0,0);}
    dispatch_single_command(command);
    unsafe{permissions_set_identity(old_uid,old_gid);}
}

fn cmd_login(args: &[u8]) {
    let target = trim_slice(args);
    if target.iter().any(|&b| b == b' ' || b == b'\t') {
        puts(b"Usage: login [username]\n");
        return;
    }
    let mut target_name = [0u8; 24];
    if target.is_empty() {
        puts(b"Username: ");
        let mut len = 0usize;
        loop {
            let key = unsafe { get_char() };
            if key == b'\r' || key == b'\n' { print_char(b'\n'); break; }
            if key == 8 || key == 127 {
                if len > 0 { len -= 1; target_name[len] = 0; puts(b"\x08 \x08"); }
                continue;
            }
            if key >= 0x20 && key < 0x7f && len < 23 {
                target_name[len] = key; len += 1; print_char(key);
            }
        }
        if len == 0 { return; }
    } else if target.len() < 24 {
        target_name[..target.len()].copy_from_slice(target);
    } else {
        puts(b"login: invalid username\n");
        return;
    }
    let mut uid = 0;
    let mut gid = 0;
    let mut flags = 0;
    if unsafe { accounts_lookup(target_name.as_ptr(), &mut uid, &mut gid, &mut flags) } != 0 {
        puts(b"login: unknown account\n");
        return;
    }
    let (mut password, len) = read_secret(b"Password: ");
    password[len] = 0;
    if unsafe { accounts_authenticate(target_name.as_ptr(), password.as_ptr()) } != 0 {
        puts(b"login: authentication failed\n");
        return;
    }
    unsafe { permissions_set_identity(uid, gid); }
    puts(b"Logged in as ");
    puts(&target_name[..target_name.iter().position(|&b| b == 0).unwrap_or(24)]);
    print_char(b'\n');

    // cd to home directory if it exists
    let mut home = [0u8; 64];
    home[..6].copy_from_slice(b"/HOME/");
    let name_len = target_name.iter().position(|&b| b == 0).unwrap_or(24);
    if name_len + 6 < 63 {
        // Copy name as uppercase for FAT/ext2 compatibility
        for i in 0..name_len {
            let c = target_name[i];
            home[6 + i] = if c >= b'a' && c <= b'z' { c - 32 } else { c };
        }
        home[6 + name_len] = 0;
        unsafe {
            let mut probe = core::mem::MaybeUninit::<VfsFile>::uninit();
            if vfs_find_file(home.as_ptr(), probe.as_mut_ptr()) == 0 {
                vfs_chdir(home.as_ptr());
            }
        }
    }
}

fn cd_user_home(target_name: &[u8; 24]) {
    let mut home = [0u8; 64];
    home[..6].copy_from_slice(b"/HOME/");
    let name_len = target_name.iter().position(|&b| b == 0).unwrap_or(24);
    if name_len + 6 < 63 {
        for i in 0..name_len {
            let c = target_name[i];
            home[6 + i] = if c >= b'a' && c <= b'z' { c - 32 } else { c };
        }
        home[6 + name_len] = 0;
        unsafe {
            let mut probe = core::mem::MaybeUninit::<VfsFile>::uninit();
            if vfs_find_file(home.as_ptr(), probe.as_mut_ptr()) == 0 {
                vfs_chdir(home.as_ptr());
                return;
            }
        }
    }
    unsafe { vfs_chdir(b"/\0".as_ptr()); }
}

fn run_login_manager() {
    puts(b"========================================\n");
    puts_color(b" CykOS Login Manager\n", 0x0B);
    puts(b"========================================\n");

    loop {
        puts(b"cykos login: ");
        let mut target_name = [0u8; 24];
        let mut len = 0usize;
        loop {
            let key = unsafe { get_char() };
            if key == 0x03 {
                puts(b"^C\n");
                len = 0;
                break;
            }
            if key == b'\r' || key == b'\n' {
                print_char(b'\n');
                break;
            }
            if key == 8 || key == 127 {
                if len > 0 {
                    len -= 1;
                    target_name[len] = 0;
                    puts(b"\x08 \x08");
                }
                continue;
            }
            if key >= 0x20 && key < 0x7f && len < 23 {
                target_name[len] = key;
                len += 1;
                print_char(key);
            }
        }
        if len == 0 { continue; }

        let mut uid = 0;
        let mut gid = 0;
        let mut flags = 0;
        if unsafe { accounts_lookup(target_name.as_ptr(), &mut uid, &mut gid, &mut flags) } != 0 {
            let (_pw, _) = read_secret(b"Password: ");
            puts_color(b"Login incorrect\n\n", 0x0C);
            continue;
        }

        let has_pw = unsafe { accounts_has_password(target_name.as_ptr()) };
        if has_pw == 1 {
            let (mut password, pw_len) = read_secret(b"Password: ");
            password[pw_len] = 0;
            if unsafe { accounts_authenticate(target_name.as_ptr(), password.as_ptr()) } != 0 {
                puts_color(b"Login incorrect\n\n", 0x0C);
                continue;
            }
        } else {
            puts_color(b"No password configured for this account.\n", 0x0E);
            let (mut password, pw_len) = read_secret(b"Set password (Enter to skip): ");
            password[pw_len] = 0;
            if pw_len > 0 {
                unsafe {
                    permissions_set_identity(0, 0);
                    if accounts_set_password(target_name.as_ptr(), password.as_ptr()) == 0 {
                        puts_color(b"Password updated.\n", 0x0A);
                    }
                }
            }
        }

        unsafe { permissions_set_identity(uid, gid); }
        cd_user_home(&target_name);
        puts_color(b"Welcome, ", 0x0A);
        puts_color(&target_name[..len], 0x0A);
        puts(b"!\n\n");
        break;
    }
}

fn cmd_logout(_args: &[u8]) {
    unsafe {
        permissions_set_identity(0, 0);
        console_clear();
    }
    run_login_manager();
}

fn cmd_who(_args: &[u8]) {
    let uid = unsafe { permissions_uid() };
    let mut name = [0u8; 24];
    if username_for_uid(uid, &mut name) {
        puts(&name[..name.iter().position(|&b| b == 0).unwrap_or(24)]);
    } else {
        puts(b"uid-"); print_u32(uid);
    }
    puts(b"       console\n");
}

fn cmd_groups(_args: &[u8]) {
    let uid = unsafe { permissions_uid() };
    let gid = unsafe { permissions_gid() };
    let mut name = [0u8; 24];
    if username_for_uid(uid, &mut name) {
        puts(&name[..name.iter().position(|&b| b == 0).unwrap_or(24)]);
    } else {
        puts(b"uid-"); print_u32(uid);
    }
    puts(b" : gid-");
    print_u32(gid);
    if uid == 0 { puts(b" root wheel"); }
    print_char(b'\n');
}

fn cmd_ps(_args: &[u8]) {
    let mut tasks = [SchedTaskInfo { pid: 0, state: 0, name: [0u8; 24], total_ticks: 0 }; 16];
    let count = unsafe { sched_list_tasks(tasks.as_mut_ptr(), 16) };
    puts(b"PID   STATE     TICKS     NAME\n");
    for i in 0..(count.max(0) as usize) {
        let t = &tasks[i];
        print_u32(t.pid);
        puts(b"     ");
        let state_str: &[u8] = match t.state {
            1 => b"READY     ",
            2 => b"RUNNING   ",
            3 => b"BLOCKED   ",
            4 => b"ZOMBIE    ",
            _ => b"UNKNOWN   ",
        };
        puts(state_str);
        print_u32(t.total_ticks);
        puts(b"         ");
        let name_len = t.name.iter().position(|&b| b == 0).unwrap_or(24);
        puts(&t.name[..name_len]);
        print_char(b'\n');
    }
}

fn print_ipv4(ip: u32) {
    print_u32((ip >> 24) & 0xFF);
    unsafe { print_char(b'.'); }
    print_u32((ip >> 16) & 0xFF);
    unsafe { print_char(b'.'); }
    print_u32((ip >> 8) & 0xFF);
    unsafe { print_char(b'.'); }
    print_u32(ip & 0xFF);
}

fn parse_ipv4(s: &[u8]) -> Option<u32> {
    if s.is_empty() { return None; }
    let mut parts = [0u32; 4];
    let mut part_idx = 0usize;
    let mut cur = 0u32;
    let mut has_digit = false;
    for &b in s {
        if b == b'.' {
            if !has_digit || part_idx >= 3 || cur > 255 { return None; }
            parts[part_idx] = cur;
            part_idx += 1;
            cur = 0;
            has_digit = false;
        } else if b >= b'0' && b <= b'9' {
            cur = cur * 10 + (b - b'0') as u32;
            if cur > 255 { return None; }
            has_digit = true;
        } else {
            return None;
        }
    }
    if !has_digit || part_idx != 3 || cur > 255 { return None; }
    parts[3] = cur;
    Some((parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3])
}

fn split_first_word(s: &[u8]) -> (&[u8], &[u8]) {
    let t = trim_slice(s);
    let pos = t.iter().position(|&b| b == b' ' || b == b'\t').unwrap_or(t.len());
    (&t[..pos], trim_slice(&t[pos..]))
}

fn cmd_ifconfig(args: &[u8]) {
    let trimmed = trim_slice(args);
    let mut info = NetInfo {
        available: 0, link_up: 0, mac: [0; 6],
        ip: 0, netmask: 0, gateway: 0, dns: 0,
        rx_packets: 0, tx_packets: 0, hw_name: [0; 32],
    };
    if unsafe { net_get_info(&mut info as *mut NetInfo) } != 0 || info.available == 0 {
        puts(b"ifconfig: no network interface available\n");
        return;
    }

    if !trimmed.is_empty() {
        let (ip_tok, rest1) = split_first_word(trimmed);
        let (mask_tok, rest2) = split_first_word(rest1);
        let (gw_tok, _) = split_first_word(rest2);

        let ip = match parse_ipv4(ip_tok) {
            Some(v) => v,
            None => {
                puts(b"Usage: ifconfig [<ip> [netmask] [gateway]]\n");
                return;
            }
        };
        let mask = if mask_tok.is_empty() {
            0xFFFFFF00u32
        } else {
            match parse_ipv4(mask_tok) {
                Some(v) => v,
                None => { puts(b"ifconfig: invalid netmask\n"); return; }
            }
        };
        let gw = if gw_tok.is_empty() {
            0u32
        } else {
            match parse_ipv4(gw_tok) {
                Some(v) => v,
                None => { puts(b"ifconfig: invalid gateway\n"); return; }
            }
        };
        if unsafe { net_set_config(ip, mask, gw) } == 0 {
            puts(b"Interface configured: ");
            print_ipv4(ip);
            puts(b"\n");
        } else {
            puts(b"ifconfig: failed to configure interface\n");
        }
        return;
    }

    let hw_len = info.hw_name.iter().position(|&b| b == 0).unwrap_or(32);
    puts(b"eth0: ");
    puts(&info.hw_name[..hw_len]);
    if info.link_up != 0 {
        puts(b" [LINK UP]\n");
    } else {
        puts(b" [LINK DOWN]\n");
    }
    puts(b"  MAC:     ");
    for i in 0..6 {
        print_hex_byte(info.mac[i]);
        if i < 5 { unsafe { print_char(b':'); } }
    }
    puts(b"\n  IPv4:    ");
    print_ipv4(info.ip);
    puts(b"\n  Netmask: ");
    print_ipv4(info.netmask);
    puts(b"\n  Gateway: ");
    print_ipv4(info.gateway);
    puts(b"\n  DNS:     ");
    print_ipv4(info.dns);
    puts(b"\n  RX pkts: ");
    print_u32(info.rx_packets);
    puts(b"  TX pkts: ");
    print_u32(info.tx_packets);
    unsafe { print_char(b'\n'); }
}

fn cmd_dhcp(_args: &[u8]) {
    puts(b"Sending DHCP discovery...\n");
    if unsafe { net_dhcp() } == 0 {
        let mut info = NetInfo {
            available: 0, link_up: 0, mac: [0; 6],
            ip: 0, netmask: 0, gateway: 0, dns: 0,
            rx_packets: 0, tx_packets: 0, hw_name: [0; 32],
        };
        unsafe { net_get_info(&mut info as *mut NetInfo); }
        puts(b"DHCP lease acquired: ");
        print_ipv4(info.ip);
        puts(b" (gw ");
        print_ipv4(info.gateway);
        puts(b")\n");
    } else {
        puts(b"dhcp: failed or timed out\n");
    }
}

fn cmd_ping(args: &[u8]) {
    let (ip_tok, rest) = split_first_word(args);
    if ip_tok.is_empty() {
        puts(b"Usage: ping <a.b.c.d> [count]\n");
        return;
    }
    let dst_ip = match parse_ipv4(ip_tok) {
        Some(v) => v,
        None => {
            puts(b"ping: invalid IPv4 address\n");
            return;
        }
    };
    let mut count = 4u32;
    let (cnt_tok, _) = split_first_word(rest);
    if !cnt_tok.is_empty() {
        let mut parsed = 0u32;
        let mut ok = true;
        for &b in cnt_tok {
            if b >= b'0' && b <= b'9' {
                parsed = parsed * 10 + (b - b'0') as u32;
            } else {
                ok = false;
                break;
            }
        }
        if ok && parsed >= 1 && parsed <= 16 {
            count = parsed;
        }
    }

    puts(b"PING ");
    print_ipv4(dst_ip);
    puts(b": 64 data bytes\n");

    let mut received = 0u32;
    for seq in 1..=count {
        let mut rtt_ms = 0u32;
        let res = unsafe { net_ping(dst_ip, seq as u16, 1500, &mut rtt_ms as *mut u32) };
        if res == 0 {
            received += 1;
            puts(b"64 bytes from ");
            print_ipv4(dst_ip);
            puts(b": icmp_seq=");
            print_u32(seq);
            puts(b" ttl=64 time=");
            print_u32(rtt_ms);
            puts(b" ms\n");
        } else {
            puts(b"Request timed out for icmp_seq ");
            print_u32(seq);
            unsafe { print_char(b'\n'); }
        }
    }
    puts(b"--- ping statistics ---\n");
    print_u32(count);
    puts(b" packets transmitted, ");
    print_u32(received);
    puts(b" received\n");
}

fn cmd_filter(args: &[u8], mode: u8) {
    let trimmed = trim_slice(args);
    let (pattern, filename) = if mode == b'g' {
        let split = trimmed.iter().position(|&b| b == b' ' || b == b'\t').unwrap_or(trimmed.len());
        if split == trimmed.len() { (trimmed, &b""[..]) } else { (&trimmed[..split], trim_slice(&trimmed[split..])) }
    } else { (&b""[..], trimmed) };
    if mode == b'g' && pattern.is_empty() {
        puts(b"Usage: grep <text> [file]\n"); return;
    }
    let bytes = unsafe {
        if filename.is_empty() { syscall_pipe_read(SHELL_FILE_BUF.as_mut_ptr(), 32768) }
        else {
            let mut path = [0u8; 64]; let n = filename.len().min(63);
            path[..n].copy_from_slice(&filename[..n]); path[n] = 0;
            vfs_read_file_by_name(path.as_ptr(), SHELL_FILE_BUF.as_mut_ptr(), 32768)
        }
    };
    if bytes < 0 { puts(b"grep: cannot read input\n"); return; }
    let mut start = 0usize; let mut i = 0usize; let mut shown = 0usize;
    while i <= bytes as usize {
        if i == bytes as usize || unsafe { SHELL_FILE_BUF[i] == b'\n' } {
            let line = unsafe { &SHELL_FILE_BUF[start..i] };
            let keep = if mode == b'g' { line.windows(pattern.len()).any(|w| bytes_eq(w, pattern)) }
                       else if mode == b'h' { shown < 10 } else { true };
            if keep { puts(line); print_char(b'\n'); shown += 1; }
            start = i + 1;
        }
        i += 1;
    }
}

fn cmd_grep(args: &[u8]) { cmd_filter(args, b'g'); }
fn cmd_head(args: &[u8]) { cmd_filter(args, b'h'); }
fn cmd_tail(args: &[u8]) {
    let filename = trim_slice(args);
    let bytes = unsafe {
        if filename.is_empty() { syscall_pipe_read(SHELL_FILE_BUF.as_mut_ptr(), 32768) }
        else { let mut path=[0u8;64]; let n=filename.len().min(63); path[..n].copy_from_slice(&filename[..n]); path[n]=0; vfs_read_file_by_name(path.as_ptr(), SHELL_FILE_BUF.as_mut_ptr(),32768) }
    };
    if bytes < 0 { puts(b"Usage: tail [file]\n"); return; }
    let mut lines=0usize; let mut i=bytes as usize;
    while i>0 { i-=1; if unsafe { SHELL_FILE_BUF[i]==b'\n' } { lines+=1; if lines==10 { i+=1; break; } } }
    unsafe { for j in i..bytes as usize { print_char(SHELL_FILE_BUF[j]); } }
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

fn cmd_wait(_args: &[u8]) {
    let mut status = 0i32;
    let pid: u32;
    unsafe {
        core::arch::asm!(
            "int 0x80",
            inout("eax") SYS_WAIT => pid,
            in("ebx") (&mut status as *mut i32 as u32)
        );
    }
    if pid == u32::MAX {
        puts(b"wait: no exited application to collect\n");
    } else {
        puts(b"[process ");
        print_u32(pid);
        puts(b"] exit status ");
        if status < 0 {
            print_char(b'-');
            print_u32(status.wrapping_neg() as u32);
        } else {
            print_u32(status as u32);
        }
        print_char(b'\n');
    }
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
        name: b"wait",
        description: b"Collect the most recently exited app's status",
        handler: cmd_wait,
    },
    Command {
        name: b"WAIT",
        description: b"",
        handler: cmd_wait,
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
    Command { name: b"echo", description: b"Print text (e.g. echo hello)", handler: cmd_echo },
    Command { name: b"ECHO", description: b"", handler: cmd_echo },
    Command { name: b"id", description: b"Show the current numeric UID and GID", handler: cmd_id },
    Command { name: b"ID", description: b"", handler: cmd_id },
    Command { name: b"whoami", description: b"Show the current user identity", handler: cmd_whoami },
    Command { name: b"WHOAMI", description: b"", handler: cmd_whoami },
    Command { name: b"chmod", description: b"Set file mode using octal permissions", handler: cmd_chmod },
    Command { name: b"CHMOD", description: b"", handler: cmd_chmod },
    Command { name: b"chown", description: b"Set numeric file owner and group (root only)", handler: cmd_chown },
    Command { name: b"CHOWN", description: b"", handler: cmd_chown },
    Command { name: b"umask", description: b"Show or set the file creation mask", handler: cmd_umask },
    Command { name: b"UMASK", description: b"", handler: cmd_umask },
    Command { name: b"stat", description: b"Show file mode, owner, group, and size", handler: cmd_stat },
    Command { name: b"STAT", description: b"", handler: cmd_stat },
    Command { name: b"access", description: b"Check read, write, or execute access", handler: cmd_access },
    Command { name: b"ACCESS", description: b"", handler: cmd_access },
    Command { name: b"runas", description: b"Run an app with numeric UID/GID (root only)", handler: cmd_runas },
    Command { name: b"RUNAS", description: b"", handler: cmd_runas },
    Command { name: b"users", description: b"List local accounts", handler: cmd_users },
    Command { name: b"USERS", description: b"", handler: cmd_users },
    Command { name: b"useradd", description: b"Create a local account (root only)", handler: cmd_useradd },
    Command { name: b"USERADD", description: b"", handler: cmd_useradd },
    Command { name: b"userdel", description: b"Delete a local account (root only)", handler: cmd_userdel },
    Command { name: b"USERDEL", description: b"", handler: cmd_userdel },
    Command { name: b"usermod", description: b"Change sudo access (root only)", handler: cmd_usermod },
    Command { name: b"USERMOD", description: b"", handler: cmd_usermod },
    Command { name: b"passwd", description: b"Set an account password", handler: cmd_passwd },
    Command { name: b"PASSWD", description: b"", handler: cmd_passwd },
    Command { name: b"su", description: b"Switch to a local account", handler: cmd_su },
    Command { name: b"SU", description: b"", handler: cmd_su },
    Command { name: b"sudo", description: b"Run a command as root", handler: cmd_sudo },
    Command { name: b"SUDO", description: b"", handler: cmd_sudo },
    Command { name: b"login", description: b"Log in as a local account", handler: cmd_login },
    Command { name: b"LOGIN", description: b"", handler: cmd_login },
    Command { name: b"logout", description: b"Log out and return to the login manager", handler: cmd_logout },
    Command { name: b"LOGOUT", description: b"", handler: cmd_logout },
    Command { name: b"who", description: b"Show the currently logged-in user", handler: cmd_who },
    Command { name: b"WHO", description: b"", handler: cmd_who },
    Command { name: b"groups", description: b"Show group memberships for the current user", handler: cmd_groups },
    Command { name: b"GROUPS", description: b"", handler: cmd_groups },
    Command { name: b"ps", description: b"List scheduled kernel/user tasks", handler: cmd_ps },
    Command { name: b"PS", description: b"", handler: cmd_ps },
    Command { name: b"ifconfig", description: b"Show or configure network interface", handler: cmd_ifconfig },
    Command { name: b"IFCONFIG", description: b"", handler: cmd_ifconfig },
    Command { name: b"dhcp", description: b"Acquire IPv4 configuration via DHCP", handler: cmd_dhcp },
    Command { name: b"DHCP", description: b"", handler: cmd_dhcp },
    Command { name: b"ping", description: b"Send ICMP echo requests to an IPv4 host", handler: cmd_ping },
    Command { name: b"PING", description: b"", handler: cmd_ping },
    Command { name: b"grep", description: b"Print lines containing text (grep text [file])", handler: cmd_grep },
    Command { name: b"GREP", description: b"", handler: cmd_grep },
    Command { name: b"head", description: b"Print the first 10 lines (head [file])", handler: cmd_head },
    Command { name: b"HEAD", description: b"", handler: cmd_head },
    Command { name: b"tail", description: b"Print the last 10 lines (tail [file])", handler: cmd_tail },
    Command { name: b"TAIL", description: b"", handler: cmd_tail },
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

    if filename.len() >= 128 {
        puts(b"CykShell: script path is too long\n");
        unsafe { SCRIPT_ACTIVE = false; }
        return;
    }
    let mut permission_path = [0u8; 128];
    permission_path[..filename.len()].copy_from_slice(filename);
    if unsafe { permissions_check(permission_path.as_ptr(), 1) } != 0 {
        puts(b"CykShell: script permission denied\n");
        unsafe { SCRIPT_ACTIVE = false; }
        return;
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

// AUTOEXEC.SHE is an optional, plain-text CykShell startup command list.
// Probe first so an absent file does not produce a startup error message.
fn run_startup_script() {
    const AUTOEXEC_PATH: &[u8] = b"/ETC/AUTOEXEC.SHE";
    let mut path = [0u8; 64];
    let mut probe = [0u8; 1];
    for i in 0..AUTOEXEC_PATH.len() { path[i] = AUTOEXEC_PATH[i]; }
    path[AUTOEXEC_PATH.len()] = 0;

    if unsafe { vfs_read_file_by_name(path.as_ptr(), probe.as_mut_ptr(), probe.len() as u32) } >= 0 {
        run_script(AUTOEXEC_PATH);
    }
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

fn dispatch_pipeline(raw_line: &[u8]) -> bool {
    let trimmed = trim_slice(raw_line);
    if trimmed.is_empty() {
        return true;
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
                return false;
            }
            if stage_count == MAX_PIPE_STAGES {
                puts(b"CykShell: pipeline supports at most 8 commands\n");
                return false;
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
                return false;
            }
            stages[stage_count] = (stage_start, index);
            stage_count += 1;
            stage_start = index + 1;
        }
        index += 1;
    }

    if stage_count == 1 && unsafe { !REDIRECT_ACTIVE } {
        return dispatch_single_command(trimmed);
    }

    for &(start, end) in &stages[..stage_count] {
        if trim_slice(&trimmed[start..end]).is_empty() {
            puts(b"CykShell: empty command in pipeline\n");
            return false;
        }
    }

    let mut input_ptr = core::ptr::null();
    let mut input_len = 0u32;
    let mut final_output_len = 0u32;
    let mut command_ok = true;
    for stage_index in 0..stage_count {
        let (start, end) = stages[stage_index];
        let stage = trim_slice(&trimmed[start..end]);
        let output_ptr = if stage_index + 1 < stage_count {
            unsafe { PIPE_BUFFERS[stage_index % 2].as_mut_ptr() }
        } else if unsafe { REDIRECT_ACTIVE } {
            unsafe { PIPE_BUFFERS[stage_count % 2].as_mut_ptr() }
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
        if !dispatch_single_command(stage) { command_ok = false; }
        let output_len = unsafe {
            PIPE_CAPTURE_ACTIVE = false;
            syscall_pipe_end()
        };
        if stage_index + 1 == stage_count { final_output_len = output_len; }

        if stage_index + 1 < stage_count {
            input_ptr = unsafe { PIPE_BUFFERS[stage_index % 2].as_ptr() };
            input_len = output_len;
        }
    }
    unsafe {
        if REDIRECT_ACTIVE {
            let out = PIPE_BUFFERS[stage_count % 2].as_ptr();
            let mut out_len = final_output_len as usize;
            if REDIRECT_APPEND {
                let old_len = vfs_read_file_by_name(REDIRECT_PATH.as_ptr(), SHELL_FILE_BUF.as_mut_ptr(), 32768);
                if old_len > 0 {
                    let copy = (old_len as usize).min(32768usize.saturating_sub(out_len));
                    let mut j = out_len;
                    while j > 0 { j -= 1; PIPE_BUFFERS[stage_count % 2][j + copy] = *out.add(j); }
                    PIPE_BUFFERS[stage_count % 2][..copy].copy_from_slice(&SHELL_FILE_BUF[..copy]);
                    out_len += copy;
                }
            }
            if vfs_write_file(REDIRECT_PATH.as_ptr(), out, out_len as u32) != 0 { puts(b"shell: redirection write failed\n"); }
            REDIRECT_ACTIVE = false;
            REDIRECT_APPEND = false;
        }
    }
    command_ok
}

fn dispatch_command(raw_line: &[u8]) {
    let line = trim_slice(raw_line);
    let mut start = 0usize;
    let mut quote = 0u8;
    let mut previous_ok = true;
    let mut next_condition = 0u8;
    let mut i = 0usize;
    while i <= line.len() {
        let end = i == line.len();
        let op = if end { 0 } else if quote == 0 && line[i] == b'&' && i+1 < line.len() && line[i+1] == b'&' { b'&' }
                 else if quote == 0 && line[i] == b'|' && i+1 < line.len() && line[i+1] == b'|' { b'|' } else { 0 };
        if !end && quote == 0 && (line[i] == b'\'' || line[i] == b'"') { quote = line[i]; }
        else if !end && quote != 0 && line[i] == quote { quote = 0; }
        if end || op != 0 {
            let mut segment = trim_slice(&line[start..i]);
            let should_run = next_condition == 0 || (next_condition == b'&' && previous_ok) || (next_condition == b'|' && !previous_ok);
            if segment.is_empty() { puts(b"CykShell: missing command near conditional operator\n"); return; }
            let mut redirect_pos = None; let mut q = 0u8; let mut k=0;
            while k<segment.len() { let c=segment[k]; if q!=0 { if c==q {q=0;} } else if c==b'\''||c==b'"' {q=c;} else if c==b'>' { redirect_pos=Some((k, k+1<segment.len() && segment[k+1]==b'>')); break; } k+=1; }
            if should_run { if let Some((pos, append)) = redirect_pos {
                let mut name_start=pos+if append {2} else {1};
                let command=trim_slice(&segment[..pos]);
                while name_start<segment.len() && (segment[name_start]==b' '||segment[name_start]==b'\t') {name_start+=1;}
                let name=trim_slice(&segment[name_start..]);
                if command.is_empty() || name.is_empty() || name.len()>63 { puts(b"Usage: command >|>> file\n"); return; }
                unsafe { REDIRECT_PATH=[0;64]; REDIRECT_PATH[..name.len()].copy_from_slice(name); REDIRECT_PATH[name.len()]=0; REDIRECT_ACTIVE=true; REDIRECT_APPEND=append; }
                segment=command;
            } }
            if should_run { previous_ok = dispatch_pipeline(segment); }
            if end { break; }
            next_condition = op;
            i += 1;
            start = i + 1;
        }
        i += 1;
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
    b"wait",
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
    b"id",
    b"whoami",
    b"chmod",
    b"chown",
    b"umask",
    b"stat",
    b"access",
    b"runas",
    b"users",
    b"useradd",
    b"userdel",
    b"usermod",
    b"passwd",
    b"su",
    b"sudo",
    b"login",
    b"who",
    b"groups",
];

const PATH_COMPLETION_CAPACITY: usize = 128;
static mut PATH_COMPLETION_COUNT: usize = 0;
static mut PATH_COMPLETION_NAMES: [[u8; 13]; PATH_COMPLETION_CAPACITY] =
    [[0; 13]; PATH_COMPLETION_CAPACITY];
static mut PATH_COMPLETION_LENGTHS: [u8; PATH_COMPLETION_CAPACITY] =
    [0; PATH_COMPLETION_CAPACITY];
static mut PATH_COMPLETION_DIRS: [u8; PATH_COMPLETION_CAPACITY] =
    [0; PATH_COMPLETION_CAPACITY];

extern "C" fn path_completion_callback(name_ptr: *const u8, _size: u32, is_dir: u8) {
    if name_ptr.is_null() { return; }
    unsafe {
        if PATH_COMPLETION_COUNT >= PATH_COMPLETION_CAPACITY { return; }
        let mut len = 0;
        while len < 12 && *name_ptr.add(len) != 0 { len += 1; }
        if len == 0 || (len == 1 && *name_ptr == b'.') ||
           (len == 2 && *name_ptr == b'.' && *name_ptr.add(1) == b'.') { return; }

        let index = PATH_COMPLETION_COUNT;
        for i in 0..len { PATH_COMPLETION_NAMES[index][i] = *name_ptr.add(i); }
        PATH_COMPLETION_LENGTHS[index] = len as u8;
        PATH_COMPLETION_DIRS[index] = is_dir;
        PATH_COMPLETION_COUNT += 1;
    }
}

fn complete_path_token(token: &[u8], output: &mut [u8; 128]) -> Option<usize> {
    let mut leaf_start = 0;
    let mut last_separator = None;
    for (i, &byte) in token.iter().enumerate() {
        if byte == b'/' || byte == b'\\' { last_separator = Some(i); }
    }
    let directory_path: &[u8] = if let Some(separator) = last_separator {
        leaf_start = separator + 1;
        if separator == 0 { &token[..1] } else { &token[..separator] }
    } else {
        &[]
    };
    if directory_path.len() >= 128 { return None; }

    let mut directory = [0u8; 128];
    for i in 0..directory_path.len() { directory[i] = directory_path[i]; }
    directory[directory_path.len()] = 0;

    unsafe {
        PATH_COMPLETION_COUNT = 0;
        let dir_ptr = if directory_path.is_empty() { core::ptr::null() } else { directory.as_ptr() };
        if vfs_list_directory(dir_ptr, path_completion_callback) < 0 { return None; }

        let prefix = &token[leaf_start..];
        let mut first_match = None;
        let mut match_count = 0usize;
        let mut common_len = 0usize;
        for index in 0..PATH_COMPLETION_COUNT {
            let candidate_len = PATH_COMPLETION_LENGTHS[index] as usize;
            if candidate_len < prefix.len() { continue; }
            let mut matches = true;
            for i in 0..prefix.len() {
                if PATH_COMPLETION_NAMES[index][i].to_ascii_lowercase() !=
                   prefix[i].to_ascii_lowercase() {
                    matches = false;
                    break;
                }
            }
            if !matches { continue; }

            if first_match.is_none() {
                first_match = Some(index);
                common_len = candidate_len;
            } else {
                let first = first_match.unwrap();
                common_len = common_len.min(candidate_len);
                let mut i = prefix.len();
                while i < common_len &&
                      PATH_COMPLETION_NAMES[first][i].to_ascii_lowercase() ==
                      PATH_COMPLETION_NAMES[index][i].to_ascii_lowercase() {
                    i += 1;
                }
                common_len = i;
            }
            match_count += 1;
        }

        let first = first_match?;
        if match_count == 1 {
            common_len = PATH_COMPLETION_LENGTHS[first] as usize;
        }
        // No common extension means Tab leaves the input untouched.
        if match_count > 1 && common_len <= prefix.len() { return None; }

        let suffix_len = if match_count == 1 { 1 } else { 0 };
        if leaf_start + common_len + suffix_len > 127 { return None; }

        let mut out_len = leaf_start;
        for i in 0..leaf_start { output[i] = token[i]; }
        for i in 0..common_len {
            output[out_len] = PATH_COMPLETION_NAMES[first][i];
            out_len += 1;
        }
        if match_count == 1 {
            if PATH_COMPLETION_DIRS[first] != 0 {
                output[out_len] = b'/';
                out_len += 1;
            } else {
                output[out_len] = b' ';
                out_len += 1;
            }
        }
        Some(out_len)
    }
}

fn print_shell_prompt() {
    // Show username@CykOS in color, then cwd
    let uid = unsafe { permissions_uid() };
    let mut name = [0u8; 24];
    if username_for_uid(uid, &mut name) {
        let name_len = name.iter().position(|&b| b == 0).unwrap_or(24);
        puts_color(&name[..name_len], 0x0A); // green
    } else if uid == 0 {
        puts_color(b"root", 0x0A);
    } else {
        puts_color(b"uid-", 0x0A);
        // Print uid digits in green
        let mut buf = [0u8; 10];
        let mut idx = 0;
        let mut n = uid;
        if n == 0 { buf[0] = b'0'; idx = 1; }
        else { while n > 0 { buf[idx] = b'0' + (n % 10) as u8; n /= 10; idx += 1; } }
        while idx > 0 { idx -= 1; puts_color(&buf[idx..idx+1], 0x0A); }
    }
    puts_color(b"@CykOS", 0x0B); // cyan
    puts(b":");

    let mut cwd_buf = [0u8; 128];
    let cwd_len = unsafe { vfs_getcwd(cwd_buf.as_mut_ptr(), 128) };
    if cwd_len > 0 {
        puts_color(&cwd_buf[..cwd_len as usize], 0x0E); // yellow
    } else {
        puts_color(b"/", 0x0E);
    }
    if uid == 0 {
        puts(b"# ");
    } else {
        puts(b"$ ");
    }
}

#[no_mangle]
pub extern "C" fn shell_main() -> ! {
    puts(b"\n========================================\n");
    puts_color(b" CykOS Interactive Shell (CykShell v0.2)\n", 0x0A); // VGA Green
    puts(b" Type 'help' for available commands.\n");
    puts(b"========================================\n\n");

    run_startup_script();
    run_login_manager();

    loop {
        print_shell_prompt();

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
            } else if c == 0x0C { // Ctrl+L: same clear action, then restore prompt and edit line
                cmd_clear(b"");
                print_shell_prompt();
                unsafe {
                    get_cursor_pos(&mut start_x, &mut start_y);
                    for i in 0..line_len { print_char(LINE_BUF[i]); }
                    set_cursor_pos(start_x + cursor_pos as i32, start_y);
                }
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
                // Complete the first token against command names, then resolve
                // later tokens (or explicit paths) against the current VFS dir.
                if line_len > 0 && cursor_pos == line_len {
                    unsafe {
                        let mut token_start = line_len;
                        while token_start > 0 && LINE_BUF[token_start - 1] != b' ' &&
                              LINE_BUF[token_start - 1] != b'\t' {
                            token_start -= 1;
                        }
                        let token = &LINE_BUF[token_start..line_len];
                        let explicit_path = token.iter().any(|&b| b == b'/' || b == b'\\');

                        if token_start > 0 || explicit_path {
                            let mut completed = [0u8; 128];
                            if let Some(completed_len) = complete_path_token(token, &mut completed) {
                                if completed_len > token.len() {
                                    let old_len = line_len;
                                    for i in 0..completed_len {
                                        LINE_BUF[token_start + i] = completed[i];
                                    }
                                    line_len = token_start + completed_len;
                                    cursor_pos = line_len;
                                    set_cursor_pos(start_x, start_y);
                                    for i in 0..line_len { print_char(LINE_BUF[i]); }
                                    for _ in line_len..old_len { print_char(b' '); }
                                    set_cursor_pos(start_x + cursor_pos as i32, start_y);
                                }
                            }
                        } else {
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
