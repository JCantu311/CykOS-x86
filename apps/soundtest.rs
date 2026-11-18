//! Sound capability check and opt-in PC speaker playback tests.
//!
//! The default run only queries driver capabilities. Playback uses synchronous
//! kernel calls and cannot be timed out from user space, so it is opt-in.

#![no_std]
#![no_main]

use core::panic::PanicInfo;

mod cykos;
use cykos::*;

const SOUND_CAP_TONE: u32 = 0x01;
const SOUND_CAP_CHIME: u32 = 0x02;

unsafe fn argument_is(argv: *const *const u8, index: isize, expected: &[u8]) -> bool {
    if argv.is_null() {
        return false;
    }
    let argument = *argv.offset(index);
    if argument.is_null() {
        return false;
    }

    for (i, expected_byte) in expected.iter().enumerate() {
        if *argument.add(i) != *expected_byte {
            return false;
        }
    }
    *argument.add(expected.len()) == 0
}

fn show_capabilities(caps: u32) {
    println("Sound driver capabilities:");
    if caps & SOUND_CAP_TONE != 0 {
        println("  Tone playback: supported");
    } else {
        println("  Tone playback: unavailable");
    }
    if caps & SOUND_CAP_CHIME != 0 {
        println("  Chime playback: supported");
    } else {
        println("  Chime playback: unavailable");
    }
    if caps == 0 {
        println("  No sound driver is active.");
    }
}

fn play_chime(caps: u32) -> i32 {
    if caps & SOUND_CAP_CHIME == 0 {
        println("Chime test skipped: the active driver does not advertise chime support.");
        return 1;
    }

    println("Playing the driver chime...");
    let result = sound_chime();
    if result == 0 {
        println("Chime syscall returned successfully.");
        0
    } else {
        print_err("Chime syscall failed.\n");
        1
    }
}

fn play_tones(caps: u32) -> i32 {
    if caps & SOUND_CAP_TONE == 0 {
        println("Tone test skipped: the active driver does not advertise tone support.");
        return 1;
    }

    let notes: [(u32, &str); 4] = [
        (262, "C4"),
        (330, "E4"),
        (392, "G4"),
        (523, "C5"),
    ];
    for &(frequency, name) in &notes {
        print("Playing ");
        println(name);
        if sound_tone(frequency, 120) != 0 {
            print_err("Tone syscall failed.\n");
            let _ = sound_stop();
            return 1;
        }
    }

    let _ = sound_stop();
    println("Tone syscalls returned successfully.");
    0
}

#[no_mangle]
#[link_section = ".entry"]
pub extern "C" fn main(argc: isize, argv: *const *const u8) -> i32 {
    println("CykOS soundtest started.");

    let mode = unsafe {
        if argc > 1 && argument_is(argv, 1, b"chime") {
            1
        } else if argc > 1 && argument_is(argv, 1, b"tones") {
            2
        } else {
            0
        }
    };

    let result = match mode {
        1 => {
            println("Querying sound driver capabilities...");
            let caps = sound_get_caps();
            show_capabilities(caps);
            play_chime(caps)
        }
        2 => {
            println("Querying sound driver capabilities...");
            let caps = sound_get_caps();
            show_capabilities(caps);
            play_tones(caps)
        }
        _ => {
            println("No playback requested; no audio driver calls were made.");
            println("Run `soundtest chime` or `soundtest tones` to opt in to playback.");
            return 0;
        }
    };

    if result == 0 {
        println("Sound test completed.");
    } else {
        println("Sound test could not complete successfully.");
    }
    result
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! { exit(101) }
