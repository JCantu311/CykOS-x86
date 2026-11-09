#![no_std]
#![no_main]

use core::panic::PanicInfo;

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
    fn fat16_list_directory(callback: extern "C" fn(*const u8, u32, u8)) -> i32;
}

// Panic Handler for no_std freestanding environment
#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    puts("\n[KERNEL PANIC] Level 3 CykShell crashed.\n");
    loop {
        unsafe {
            core::arch::asm!("hlt");
        }
    }
}

// Standard I/O Helpers
fn puts(s: &str) {
    for b in s.bytes() {
        unsafe {
            print_char(b);
        }
    }
}

fn puts_color(s: &str, color: u8) {
    for b in s.bytes() {
        unsafe {
            // VGA color text output
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

fn str_eq(a: &str, b: &str) -> bool {
    let a_bytes = a.as_bytes();
    let b_bytes = b.as_bytes();
    if a_bytes.len() != b_bytes.len() {
        return false;
    }
    for i in 0..a_bytes.len() {
        if a_bytes[i] != b_bytes[i] {
            return false;
        }
    }
    true
}

fn trim_ascii(s: &str) -> &str {
    let bytes = s.as_bytes();
    let mut start = 0;
    while start < bytes.len() && (bytes[start] == b' ' || bytes[start] == b'\t' || bytes[start] == b'\r' || bytes[start] == b'\n') {
        start += 1;
    }
    let mut end = bytes.len();
    while end > start && (bytes[end - 1] == b' ' || bytes[end - 1] == b'\t' || bytes[end - 1] == b'\r' || bytes[end - 1] == b'\n') {
        end -= 1;
    }
    unsafe { core::str::from_utf8_unchecked(&bytes[start..end]) }
}

// =========================================================================
// Command Registry & Handlers (Scalable Command Architecture)
// =========================================================================

pub struct Command {
    pub name: &'static str,
    pub description: &'static str,
    pub handler: fn(&str),
}

// Built-in Command: clear (Linux compatible screen clear)
fn cmd_clear(_args: &str) {
    unsafe {
        clear_screen();
    }
}

// Callback invoked for each directory entry by Level 1 FAT16 driver
static mut DIR_FILE_COUNT: u32 = 0;

extern "C" fn dir_entry_callback(name_ptr: *const u8, size: u32, is_dir: u8) {
    unsafe {
        DIR_FILE_COUNT += 1;
    }

    let mut len = 0;
    while unsafe { *name_ptr.add(len) } != 0 && len < 13 {
        unsafe { print_char(*name_ptr.add(len)); }
        len += 1;
    }

    // Pad out filename column to 16 characters
    while len < 16 {
        unsafe { print_char(b' '); }
        len += 1;
    }

    if is_dir != 0 {
        puts("<DIR>\n");
    } else {
        print_u32(size);
        puts(" bytes\n");
    }
}

// Built-in Command: dir (MS-DOS compatible directory listing)
fn cmd_dir(_args: &str) {
    puts("\n Volume in drive C is CYKOS_DISK\n");
    puts(" Directory of C:\\\n\n");

    unsafe {
        DIR_FILE_COUNT = 0;
        fat16_list_directory(dir_entry_callback);
    }

    puts("\n               ");
    unsafe { print_u32(DIR_FILE_COUNT); }
    puts(" File(s)\n\n");
}

// Built-in Command: reboot (Linux compatible system reboot)
fn cmd_reboot(_args: &str) {
    puts("\nRebooting CykOS...\n");
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
fn cmd_shutdown(_args: &str) {
    puts("\nShutting down CykOS...\n");
    unsafe {
        secwait(1);

        // 1. QEMU ACPI poweroff port
        outw(0x604, 0x2000);

        // 2. Bochs / VirtualBox / older QEMU poweroff port
        outw(0xB004, 0x2000);

        // 3. APM poweroff port
        outw(0x4004, 0x3400);

        // 4. Hardware fallback
        puts("It is now safe to turn off your computer.\n");
        loop {
            core::arch::asm!("cli", "hlt");
        }
    }
}

// Built-in Command: help
fn cmd_help(_args: &str) {
    puts("\nAvailable CykShell Commands:\n");
    for cmd in COMMANDS {
        puts("  ");
        puts(cmd.name);
        let mut pad = cmd.name.len();
        while pad < 12 {
            unsafe { print_char(b' '); }
            pad += 1;
        }
        puts("- ");
        puts(cmd.description);
        puts("\n");
    }
    puts("\n");
}

// Configurable Command Registry Table
// Space is reserved below for future built-ins and user applications!
pub static COMMANDS: &[Command] = &[
    Command {
        name: "help",
        description: "Display this list of recognized commands",
        handler: cmd_help,
    },
    Command {
        name: "clear",
        description: "Clear the terminal display",
        handler: cmd_clear,
    },
    Command {
        name: "dir",
        description: "List files and directories in current volume",
        handler: cmd_dir,
    },
    Command {
        name: "reboot",
        description: "Reboot the operating system",
        handler: cmd_reboot,
    },
    Command {
        name: "shutdown",
        description: "Power off the virtual machine",
        handler: cmd_shutdown,
    },
    // Future user applications or additional built-ins can be registered here!
];

// Command Dispatcher
fn dispatch_command(raw_line: &str) {
    let trimmed = trim_ascii(raw_line);
    if trimmed.is_empty() {
        return;
    }

    // Split into command name and argument tail
    let bytes = trimmed.as_bytes();
    let mut split_idx = 0;
    while split_idx < bytes.len() && bytes[split_idx] != b' ' {
        split_idx += 1;
    }

    let cmd_name = unsafe { core::str::from_utf8_unchecked(&bytes[..split_idx]) };
    let args = if split_idx < bytes.len() {
        trim_ascii(unsafe { core::str::from_utf8_unchecked(&bytes[split_idx..]) })
    } else {
        ""
    };

    for cmd in COMMANDS {
        if str_eq(cmd.name, cmd_name) {
            (cmd.handler)(args);
            return;
        }
    }

    puts("CykShell: command not found: ");
    puts(cmd_name);
    puts("\nType 'help' to view available commands.\n");
}

// =========================================================================
// Main Interactive Shell Loop (Level 3 Entry Point)
// =========================================================================

#[no_mangle]
pub extern "C" fn shell_main() -> ! {
    puts("\n========================================\n");
    puts_color(" CykOS Interactive Shell (CykShell v0.1)\n", 0x0A); // VGA Green
    puts(" Type 'help' for available commands.\n");
    puts("========================================\n\n");

    let mut line_buf = [0u8; 128];
    let mut line_len: usize = 0;

    loop {
        puts("CykOS> ");
        line_len = 0;

        loop {
            let c = unsafe { get_char() };

            if c == b'\n' || c == b'\r' {
                unsafe { print_char(b'\n'); }
                break;
            } else if c == 0x08 { // Backspace
                if line_len > 0 {
                    line_len -= 1;
                    unsafe { print_char(0x08); }
                }
            } else if c >= 32 && c <= 126 { // Printable ASCII range
                if line_len < line_buf.len() - 1 {
                    line_buf[line_len] = c;
                    line_len += 1;
                    unsafe { print_char(c); }
                }
            }
        }

        if let Ok(line_str) = core::str::from_utf8(&line_buf[..line_len]) {
            dispatch_command(line_str);
        }
    }
}
