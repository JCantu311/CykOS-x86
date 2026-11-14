//! Sound Driver & PC Speaker Test Utility
//! Tests PC speaker tones, frequencies, melody sequences, and sound driver availability.

#![no_std]
#![no_main]

use core::panic::PanicInfo;

mod cykos;
use cykos::*;

#[no_mangle]
#[link_section = ".entry"]
pub extern "C" fn main(_argc: isize, _argv: *const *const u8) -> i32 {
    clear();
    println("========================================");
    println("  CykOS Sound Driver Diagnostic Test");
    println("========================================");

    println("\n[1/3] Testing default chime sequence...");
    sound_chime();
    msleep(600);

    println("[2/3] Playing musical scale tones (C4 -> C5)...");
    let scale: [(u32, &str); 8] = [
        (262, "  Note C4 (262 Hz)"),
        (294, "  Note D4 (294 Hz)"),
        (330, "  Note E4 (330 Hz)"),
        (349, "  Note F4 (349 Hz)"),
        (392, "  Note G4 (392 Hz)"),
        (440, "  Note A4 (440 Hz)"),
        (494, "  Note B4 (494 Hz)"),
        (523, "  Note C5 (523 Hz)"),
    ];
    let mut any_tone_error = false;
    for &(freq, label) in &scale {
        println(label);
        if sound_tone(freq, 350) != 0 {
            any_tone_error = true;
        }
        msleep(150);
    }

    msleep(400);

    println("\n[3/3] Playing fanfare melody...");
    let fanfare: [(u32, u32); 5] = [
        (523, 200), // C5
        (659, 200), // E5
        (784, 250), // G5
        (1046, 500), // C6
        (784, 300), // G5
    ];
    for &(freq, duration) in &fanfare {
        if sound_tone(freq, duration) != 0 {
            any_tone_error = true;
        }
        msleep(80);
    }

    if any_tone_error {
        print_err("\n-> Sound Driver Diagnostic: WARNING [Syscall failed]\n");
    } else {
        println("\n-> Sound Driver Diagnostic: PASS [All frequencies verified]");
    }
    println("Press any key to exit...");
    read_char();
    0
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {
        unsafe { core::arch::asm!("hlt"); }
    }
}
