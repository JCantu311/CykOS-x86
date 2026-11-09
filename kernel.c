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
#define ATA_CMD_READ        0x20
#define ATA_CMD_WRITE       0x30
#define ATA_CMD_CACHE_FLUSH 0xE7

// Status Bits
#define ATA_STATUS_ERR  0x01  // Error bit
#define ATA_STATUS_DRQ  0x08  // Data Request bit
#define ATA_STATUS_DF   0x20  // Drive Fault bit
#define ATA_STATUS_BSY  0x80  // Busy bit

// 400ns delay required after drive selection or command issuance
static void ata_delay_400ns(void) {
    inb(ATA_STATUS_PORT);
    inb(ATA_STATUS_PORT);
    inb(ATA_STATUS_PORT);
    inb(ATA_STATUS_PORT);
}

static void ata_wait_ready(void) {
    ata_delay_400ns();
    while (inb(ATA_STATUS_PORT) & ATA_STATUS_BSY);
}

// Wait for DRQ with error detection (returns 0 on success, -1 on error)
static int ata_wait_drq(void) {
    ata_delay_400ns();
    while (1) {
        unsigned char status = inb(ATA_STATUS_PORT);
        if (status & (ATA_STATUS_ERR | ATA_STATUS_DF)) {
            return -1; // Disk controller reported an error
        }
        if (!(status & ATA_STATUS_BSY) && (status & ATA_STATUS_DRQ)) {
            return 0;  // Ready to transfer data
        }
    }
}


/**
 * Reads sequential sectors using LBA28 PIO Mode
 * @param lba        The absolute sector number to start reading from
 * @param count      How many sectors to read
 * @param target_buf Target RAM address pointer to copy data into
 * @return           0 on success, -1 on failure
 */
int ata_read_sectors(unsigned int lba, unsigned char count, unsigned short* target_buf) {
    ata_wait_ready();

    // 1. Select master drive (0xE0) and send top 4 bits of the LBA (bits 24-27)
    outb(ATA_DRIVE_PORT, 0xE0 | ((lba >> 24) & 0x0F));
    ata_delay_400ns();

    // 2. Set sector count and LBA bits 0-23
    outb(ATA_SECTOR_COUNT_PORT, count);
    outb(ATA_LBA_LOW_PORT,  (unsigned char)(lba));
    outb(ATA_LBA_MID_PORT,  (unsigned char)(lba >> 8));
    outb(ATA_LBA_HIGH_PORT, (unsigned char)(lba >> 16));

    // 3. Send Read Command
    outb(ATA_COMMAND_PORT, ATA_CMD_READ);

    // 4. Read data sector by sector (each sector contains 256 words / 512 bytes)
    for (int i = 0; i < count; i++) {
        if (ata_wait_drq() != 0) {
            return -1;
        }

        for (int j = 0; j < 256; j++) {
            target_buf[(i * 256) + j] = inw(ATA_DATA_PORT);
        }
    }
    return 0;
}

/**
 * Writes sequential sectors using LBA28 PIO Mode
 * @param lba        The absolute sector number to write to
 * @param count      How many sectors to write
 * @param source_buf Source RAM address pointer containing data to write
 * @return           0 on success, -1 on failure
 */
int ata_write_sectors(unsigned int lba, unsigned char count, const unsigned short* source_buf) {
    ata_wait_ready();

    // 1. Select master drive (0xE0) and send top 4 bits of the LBA (bits 24-27)
    outb(ATA_DRIVE_PORT, 0xE0 | ((lba >> 24) & 0x0F));
    ata_delay_400ns();

    // 2. Set sector count and LBA bits 0-23
    outb(ATA_SECTOR_COUNT_PORT, count);
    outb(ATA_LBA_LOW_PORT,  (unsigned char)(lba));
    outb(ATA_LBA_MID_PORT,  (unsigned char)(lba >> 8));
    outb(ATA_LBA_HIGH_PORT, (unsigned char)(lba >> 16));
    
    // 3. Send Write Command
    outb(ATA_COMMAND_PORT, ATA_CMD_WRITE);

    // 4. Write data sector by sector
    for (int i = 0; i < count; i++) {
        if (ata_wait_drq() != 0) {
            return -1;
        }

        for (int j = 0; j < 256; j++) {
            outw(ATA_DATA_PORT, source_buf[(i * 256) + j]);
        }
    }

    // 5. Commit data to disk and wait until controller is ready
    outb(ATA_COMMAND_PORT, ATA_CMD_CACHE_FLUSH);
    ata_wait_ready();

    return 0;
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
    if (ata_write_sectors(100, 1, write_buffer) != 0) {
        return 1; // Write failed
    }

    // 3. Clear our read buffer completely to avoid false positives
    for (int i = 0; i < 256; i++) {
        read_buffer[i] = 0x0000;
    }

    // 4. Retrieve data back from raw storage
    if (ata_read_sectors(100, 1, read_buffer) != 0) {
        return 1; // Read failed
    }

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

    int disk_result = disk_test();

    if (disk_result == 0) {
        print_color_string(msg_pass, VGA_GREEN);
    } else {
        print_color_string(msg_fail, VGA_RED);
    }

    print_color_string(welcome_message, VGA_GREEN);

    // char shell_msg[] = ;
    print_string("Loading CykShell...\n");
    
    while (1) {
        asm("hlt");
    }
}
