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
    puts(b" Directory of C:\\\n\n");

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

#[no_mangle]
pub extern "C" fn shell_main() -> ! {
    puts(b"\n========================================\n");
    puts_color(b" CykOS Interactive Shell (CykShell v0.1)\n", 0x0A); // VGA Green
    puts(b" Type 'help' for available commands.\n");
    puts(b"========================================\n\n");

    let mut line_len: usize = 0;

    loop {
        puts(b"CykOS> ");
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
                unsafe {
                    if line_len < LINE_BUF.len() - 1 {
                        LINE_BUF[line_len] = c;
                        line_len += 1;
                        print_char(c);
                    }
                }
            }
        }

        unsafe {
            dispatch_command(&LINE_BUF[..line_len]);
        }
    }
}
