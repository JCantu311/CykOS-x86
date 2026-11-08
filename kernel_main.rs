#![no_std]
#![no_main]

use core::panic::PanicInfo;
use core::ptr::write_volatile;
use core::arch::asm;

#[unsafe(no_mangle)]
pub extern "C" fn kernel_main() -> ! {
    let vga_buffer = 0xb8000 as *mut u16;

    const SCREEN_SIZE: usize = 80 * 25;
    const BLANK_SPACE: u16 = 0x0F20; // Space character, white on black

    /// Tests a block of memory at the given pointer and size (in 32-bit dwords).
    /// Returns `true` if passed, `false` if a memory error was detected.
    pub unsafe fn memtest_dword(ptr: *mut u32, count: usize) -> bool {
        let mut success: u32;

        asm!(
            // Save state of EDI (Rust uses it, let's keep it safe)
            "push edi",

            // --- Phase 1: Fill Memory ---
            "2:",
            "mov [edi + ecx * 4 - 4], eax", // Write pattern to current dword
            "loop 2b",                     // Decrement ECX and loop until ECX == 0

            // --- Reset ECX for verification ---
            "mov ecx, {count}",

            // --- Phase 2: Verify Memory ---
            "3:",
            "mov edx, [edi + ecx * 4 - 4]", // Read value back
            "cmp edx, eax",                 // Is it what we expected?
            "jne 4f",                       // Mismatch! Jump forward to error handler
            "loop 3b",                     // Loop until ECX == 0

            // --- Success Path ---
            "mov {success}, 1",             // Set success indicator to true
            "jmp 5f",                       // Skip to cleanup

            // --- Error Path ---
            "4:",
            "mov {success}, 0",             // Set success indicator to false

            // --- Cleanup ---
            "5:",
            "pop edi",                      // Restore EDI register
        
            // Inputs and Outputs configuration
            count = in(reg) count,
            in("eax") 0xAA55AA55_u32,       // Pattern register
            in("edi") ptr,                  // Destination buffer pointer
            out("ecx") _,                   // ECX is modified/clobbered by loops
            out("edx") _,                   // EDX is modified/clobbered by verification
            success = out(reg) success,
            options(nostack),               // Tells compiler we manage our own stack offsets
        );

        success != 0
    }


    unsafe {
        // 1. Clear the screen using strict volatile writes
        for i in 0..SCREEN_SIZE {
            write_volatile(vga_buffer.add(i), BLANK_SPACE);
        }

        // 2. Print "OK" using volatile writes
        write_volatile(vga_buffer.add(0), 0x0F4F); // Bright White 'O'
        write_volatile(vga_buffer.add(1), 0x0F4B); // Bright White 'K'
    }

    loop {}
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {}
}
