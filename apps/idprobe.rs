#![no_std]
#![no_main]

use core::panic::PanicInfo;

mod cykos;
use cykos::*;

unsafe fn drop_identity_requested(argc: isize, argv: *const *const u8) -> bool {
    if argc < 2 || argv.is_null() { return false; }
    let arg = *argv.add(1);
    if arg.is_null() { return false; }
    let expected = b"drop\0";
    for (index, byte) in expected.iter().enumerate() {
        if *arg.add(index) != *byte { return false; }
    }
    true
}

#[no_mangle]
#[link_section = ".entry"]
pub extern "C" fn main(argc: isize, argv: *const *const u8) -> i32 {
    if unsafe { drop_identity_requested(argc, argv) } {
        if setgid(1001) != 0 || setuid(1001) != 0 {
            println("IDPROBE: could not change test identity");
            return 1;
        }
        println("IDPROBE: changed identity to 1001/1001; run again to check shell restoration");
        return 0;
    }

    if getuid() == 0 && getgid() == 0 {
        println("IDPROBE: shell identity restored (0/0)");
        return 0;
    }
    println("IDPROBE: shell identity was not restored");
    1
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! { exit(101) }
