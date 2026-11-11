# CykOS Rust Userland Application Tooling

This directory provides the standalone tooling to compile, link, and package Rust applications into CykOS without touching the operating system kernel.

## Architecture

- **Freestanding Rust**: All applications are written with `#![no_std]` and `#![no_main]`.
- **Memory Layout**: Executables are loaded at `0x50000` (`USER_APP_BASE`) in 32-bit protected mode.
- **External Linking**: Applications call kernel functions directly via `bin/kernel_syms.ld`. They are linked independently into flat binary files (`.BIN`) and never linked into the kernel ELF.
- **Packaging**: The image builder places the shell in `/bin`, applications in `/usr/bin`, drivers and their manifest in `/boot`, aliases in `/etc`, and documentation in `/usr/share/doc`.

## Command Line Arguments (`argc`, `argv`)

The CykShell and command loader pass arguments using the standard C/Rust calling convention:

```rust
#[no_mangle]
pub extern "C" fn main(argc: isize, argv: *const *const u8) -> i32 {
    // argv[0] is the command name as invoked (e.g. "edit" or "hello")
    // argv[1] is the first argument (e.g. "README.TXT")
    // argv[argc] is NULL
    0
}
```

Or using the ergonomic helper in `apps/cykos.rs`:

```rust
let args = unsafe { cykos::Args::from_raw(argc, argv) };
if let Some(filename) = args.get(1) {
    // filename is &str
}
```

## Building Applications

To build all apps and update the QEMU hard disk image:

```bash
./apps/build.sh
```

To build a specific app (e.g. `edit.rs`):

```bash
./apps/build.sh edit.rs
```

## Running in QEMU

Start QEMU:

```bash
qemu-system-i386 -drive format=raw,file=bin/os_image.bin
```

In CykShell:
- `hello` or `HELLO.BIN`
- `hello arg1 "quoted argument"`
- `edit README.TXT`
