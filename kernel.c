#include "cykastd.h"

// Define ATA Primary Bus I/O Ports
#define ATA_DATA_PORT        0x1F0
#define ATA_SECTOR_COUNT_PORT 0x1F2
#define ATA_LBA_LOW_PORT     0x1F3
#define ATA_LBA_MID_PORT     0x1F4
#define ATA_LBA_HIGH_PORT    0x1F5
#define ATA_DRIVE_PORT       0x1F6
#define ATA_COMMAND_PORT     0x1F7
#define ATA_STATUS_PORT      0x1F7

// ATA Commands
#define ATA_CMD_READ  0x20
#define ATA_CMD_WRITE 0x30

// Status Bits
#define ATA_STATUS_BSY  0x80  // Busy bit
#define ATA_STATUS_DRQ  0x08  // Data Request bit

// Extern definitions from cykastd
unsigned short inw(unsigned short port);
void outw(unsigned short port, unsigned short data);

static void ata_wait_ready(void) {
    // Read an unused port to generate a tiny hardware micro-delay
    inb(0x80); 
    while (inb(ATA_STATUS_PORT) & ATA_STATUS_BSY);
}

static void ata_wait_drq(void) {
    inb(0x80);
    while (!(inb(ATA_STATUS_PORT) & ATA_STATUS_DRQ));
}


/**
 * Reads sequential sectors using LBA28 PIO Mode
 * @param lba        The absolute sector number to start reading from
 * @param count      How many sectors to read
 * @param target_buf Target RAM address pointer to copy data into
 */
void ata_read_sectors(unsigned int lba, unsigned char count, unsigned short* target_buf) {
    ata_wait_ready();

    // Out parameters: sector count and mapping out bits 0-27 of LBA
    outb(ATA_SECTOR_COUNT_PORT, count);
    outb(ATA_LBA_LOW_PORT,  (unsigned char)(lba));
    outb(ATA_LBA_MID_PORT,  (unsigned char)(lba >> 8));
    outb(ATA_LBA_HIGH_PORT, (unsigned char)(lba >> 16));
    
    // Select master drive and send top 4 bits of the LBA
    outb(ATA_DRIVE_PORT, 0x40 | ((lba >> 24) & 0x0F));

    // Send Read Command
    outb(ATA_COMMAND_PORT, ATA_CMD_READ);

    // Read data sector by sector (each sector contains 256 words / 512 bytes)
    for (int i = 0; i < count; i++) {
        ata_wait_ready();
        ata_wait_drq();

        for (int j = 0; j < 256; j++) {
            target_buf[(i * 256) + j] = inw(ATA_DATA_PORT);
        }
    }
}

/**
 * Writes sequential sectors using LBA28 PIO Mode
 * @param lba        The absolute sector number to write to
 * @param count      How many sectors to write
 * @param source_buf Source RAM address pointer containing data to write
 */
void ata_write_sectors(unsigned int lba, unsigned char count, const unsigned short* source_buf) {
    ata_wait_ready();

    outb(ATA_SECTOR_COUNT_PORT, count);
    outb(ATA_LBA_LOW_PORT,  (unsigned char)(lba));
    outb(ATA_LBA_MID_PORT,  (unsigned char)(lba >> 8));
    outb(ATA_LBA_HIGH_PORT, (unsigned char)(lba >> 16));
    outb(ATA_DRIVE_PORT, 0xE0 | ((lba >> 24) & 0x0F));
    
    // Send Write Command
    outb(ATA_COMMAND_PORT, ATA_CMD_WRITE);

    // Write data sector by sector
    for (int i = 0; i < count; i++) {
        ata_wait_ready();
        ata_wait_drq();

        for (int j = 0; j < 256; j++) {
            outw(ATA_DATA_PORT, source_buf[(i * 256) + j]);
        }
    }
}

/**
 * Diagnostic Verification Test for Hard Disk I/O
 * @return 0 on success, 1 on failure
 */
int disk_test(void) {
    unsigned short write_buffer[256];
    unsigned short read_buffer[256];

    // 1. Initialize the write buffer with a predictable pattern
    for (int i = 0; i < 256; i++) {
        write_buffer[i] = (i % 2 == 0) ? 0xDEAF : 0xBEEF;
    }

    // 2. Commit buffer data to Sector 100 (Safe zone beyond kernel space)
    ata_write_sectors(100, 1, write_buffer);

    // 3. Clear our read buffer completely to avoid false positives
    for (int i = 0; i < 256; i++) {
        read_buffer[i] = 0x0000;
    }

    // 4. Retrieve data back from raw storage
    ata_read_sectors(100, 1, read_buffer);

    // 5. Compare the contents of the read block against what we wrote
    for (int i = 0; i < 256; i++) {
        if (read_buffer[i] != write_buffer[i]) {
            return 1; // Verification Failed
        }
    }
    return 0; // Verification Succeeded
}


int memtest_dword(volatile unsigned int* ptr, unsigned int count) {
    unsigned int pattern = 0xAA55AA55;
    for (unsigned int i = 0; i < count; i++) {
        ptr[i] = pattern;
    }
    for (unsigned int i = 0; i < count; i++) {
        if (ptr[i] != pattern) {
            return 0; 
        }
    }
    return 1; 
}


void kernel_main(void) {
    clear_screen();
    
    // Force the compiler to embed characters directly on the stack as numbers
    char msg_init[] = "CykOS Kernel Init\n";
    char msg_diag[] = "Running Memtest...\n";
    char msg_pass[] = "PASS\n";
    char msg_fail[] = "FAIL\n";

    print_string(msg_init);
    print_string(msg_diag);
    
    unsigned int* test_address = (unsigned int*)0x10000;
    
    if (memtest_dword(test_address, 1024)) {
        print_color_string(msg_pass, VGA_GREEN);
    } else {
        print_color_string(msg_fail, VGA_RED);
    }

    char cykaos_version[] = "CykOS v0.0.1\n";

    print_string(cykaos_version);

    char welcome_message[] = "Welcome to CykOS...\n";

    char disk_test_msg[] = "Running Disk Test...\n";

    print_string(disk_test_msg);

    // int disk_result = disk_test();

    if(disk_result == 1) {
        print_color_string(msg_fail, VGA_RED);
    } else if(disk_result == 0) {
        print_color_string(msg_pass, VGA_GREEN);
    }

    print_color_string(welcome_message, VGA_GREEN);
    
    while (1) {
        asm("hlt");
    }
}
