#![no_std]
#![no_main]

use core::panic::PanicInfo;

#[no_mangle]
#[link_section = ".entry"]
pub extern "C" fn main(argc: isize, argv: *const *const u8) -> i32 {
    unsafe {
        // Syscall 4: SYS_WRITE (stdout, banner, len)
        let banner = b"\n========================================\n  CykOS Syscall & IDT Demo (int 0x80)\n========================================\n\0";
        let mut banner_len = 0;
        while banner[banner_len] != 0 { banner_len += 1; }
        
        core::arch::asm!(
            "int 0x80",
            inout("eax") 4u32 => _,
            in("ebx") 1u32, // FD_STDOUT
            in("ecx") banner.as_ptr() as u32,
            in("edx") banner_len as u32,
            options(nostack)
        );

        // Print argc
        let msg_argc = b"Process received argc: \0";
        core::arch::asm!(
            "int 0x80",
            inout("eax") 4u32 => _,
            in("ebx") 1u32,
            in("ecx") msg_argc.as_ptr() as u32,
            in("edx") 23u32,
            options(nostack)
        );

        let c = b'0' + (argc as u8);
        core::arch::asm!(
            "int 0x80",
            inout("eax") 4u32 => _,
            in("ebx") 1u32,
            in("ecx") &c as *const u8 as u32,
            in("edx") 1u32,
            options(nostack)
        );
        let nl = b"\n\0";
        core::arch::asm!(
            "int 0x80",
            inout("eax") 4u32 => _,
            in("ebx") 1u32,
            in("ecx") nl.as_ptr() as u32,
            in("edx") 1u32,
            options(nostack)
        );

        // Test Syscall 45: SYS_BRK (Dynamic Heap Allocation)
        let msg_heap = b"[sysdemo] Testing SYS_BRK (Heap Allocation via int 0x80)...\n\0";
        core::arch::asm!(
            "int 0x80",
            inout("eax") 4u32 => _,
            in("ebx") 1u32,
            in("ecx") msg_heap.as_ptr() as u32,
            in("edx") 59u32,
            options(nostack)
        );

        // Allocate 1024 bytes from the 7MB kernel heap
        let heap_ptr: u32;
        core::arch::asm!(
            "int 0x80",
            inout("eax") 45u32 => heap_ptr, // SYS_BRK
            in("ebx") 1024u32,               // 1KB increment
            options(nostack)
        );

        if heap_ptr != 0 && heap_ptr != 0xFFFFFFFF {
            let msg_ok = b" -> Allocated 1024 bytes at extended heap memory: SUCCESS!\n\0";
            core::arch::asm!(
                "int 0x80",
                inout("eax") 4u32 => _,
                in("ebx") 1u32,
                in("ecx") msg_ok.as_ptr() as u32,
                in("edx") 57u32,
                options(nostack)
            );

            // Write test pattern into dynamically allocated memory
            let p = heap_ptr as *mut u8;
            for i in 0..16 {
                *p.add(i) = b'A' + (i as u8);
            }

            let msg_readback = b" -> Readback pattern from dynamic heap: \0";
            core::arch::asm!(
                "int 0x80",
                inout("eax") 4u32 => _,
                in("ebx") 1u32,
                in("ecx") msg_readback.as_ptr() as u32,
                in("edx") 40u32,
                options(nostack)
            );
            core::arch::asm!(
                "int 0x80",
                inout("eax") 4u32 => _,
                in("ebx") 1u32,
                in("ecx") p as u32,
                in("edx") 16u32,
                options(nostack)
            );
            core::arch::asm!(
                "int 0x80",
                inout("eax") 4u32 => _,
                in("ebx") 1u32,
                in("ecx") nl.as_ptr() as u32,
                in("edx") 1u32,
                options(nostack)
            );
        }

        let done = b"Syscall demo completed cleanly. Returning to shell.\n\n\0";
        core::arch::asm!(
            "int 0x80",
            inout("eax") 4u32 => _,
            in("ebx") 1u32,
            in("ecx") done.as_ptr() as u32,
            in("edx") 53u32,
            options(nostack)
        );
    }

    0
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {
        unsafe { core::arch::asm!("hlt"); }
    }
}
