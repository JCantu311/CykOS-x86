#![no_std]
#![no_main]

use core::arch::asm;
use core::panic::PanicInfo;
use core::ptr::write_volatile;

pub unsafe fn memtest_dword(ptr: *mut u32, count: usize) -> bool {
    let mut success: u32;
    unsafe {
        asm!(
            "2:",
            "mov [edi + ecx * 4 - 4], eax",
            "loop 2b",

            "mov ecx, {count}",

            "3:",
            "mov edx, [edi + ecx * 4 - 4]",
            "cmp edx, eax",
            "jne 4f",
            "loop 3b",

            "mov {success}, 1",
            "jmp 5f",

            "4:",
            "mov {success}, 0",

            "5:",
        
            count = in(reg) count,
            in("eax") 0xAA55AA55_u32,
            in("edi") ptr,
            out("ecx") _,
            out("edx") _,
            success = out(reg) success,
            options(nostack),
        );
    }
    success != 0
}

#[unsafe(no_mangle)]
pub extern "C" fn kernel_main() -> ! {
    let vga_buffer = 0xb8000 as *mut u16;

    const SCREEN_SIZE: usize = 80 * 25;
    const BLANK_SPACE: u16 = 0x0F20;

    unsafe {
        // 1. Clear the screen
        for i in 0..SCREEN_SIZE {
            write_volatile(vga_buffer.add(i), BLANK_SPACE);
        }

        // 2. Set up our test parameters (Test 1024 dwords / 4KB of RAM at address 0x10000)
        let test_address = 0x10000 as *mut u32;
        let test_count = 1024;

        // 3. Call the unsafe memtest function inside an unsafe block
        let is_memory_valid = memtest_dword(test_address, test_count);

        // 4. Update the screen based on the output
        if is_memory_valid {
            // Print "PASS" in Light Green (0x0A) text
            write_volatile(vga_buffer.add(0), 0x0A50); // 'P'
            write_volatile(vga_buffer.add(1), 0x0A41); // 'A'
            write_volatile(vga_buffer.add(2), 0x0A53); // 'S'
            write_volatile(vga_buffer.add(3), 0x0A53); // 'S'
        } else {
            // Print "FAIL" in Bright Red (0x0C) text
            write_volatile(vga_buffer.add(0), 0x0C46); // 'F'
            write_volatile(vga_buffer.add(1), 0x0C41); // 'A'
            write_volatile(vga_buffer.add(2), 0x0C49); // 'I'
            write_volatile(vga_buffer.add(3), 0x0C4C); // 'L'
        }
    }

    loop {}
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {}
}
