#include "cykastd.h"
#include "ata.h"

#define ATA_PRIMARY_BASE      0x1F0
#define ATA_SECONDARY_BASE    0x170
#define ATA_PRIMARY_CONTROL   0x3F6
#define ATA_SECONDARY_CONTROL 0x376

// ATA Commands
#define ATA_CMD_READ          0x20
#define ATA_CMD_WRITE         0x30
#define ATA_CMD_CACHE_FLUSH   0xE7
#define ATA_CMD_IDENTIFY      0xEC

// Status Bits
#define ATA_STATUS_ERR        0x01  // Error bit
#define ATA_STATUS_DRQ        0x08  // Data Request bit
#define ATA_STATUS_DF         0x20  // Drive Fault bit
#define ATA_STATUS_BSY        0x80  // Busy bit
#define ATA_POLL_LIMIT        1000000

static unsigned short ata_base_for_drive(unsigned char drive) {
    return drive < 2 ? ATA_PRIMARY_BASE : ATA_SECONDARY_BASE;
}

static unsigned short ata_control_for_drive(unsigned char drive) {
    return drive < 2 ? ATA_PRIMARY_CONTROL : ATA_SECONDARY_CONTROL;
}

static void ata_delay_400ns(unsigned short control) {
    inb(control);
    inb(control);
    inb(control);
    inb(control);
}

static int ata_wait_ready(unsigned short base, unsigned short control) {
    ata_delay_400ns(control);
    for (unsigned int timeout = 0; timeout < ATA_POLL_LIMIT; timeout++) {
        unsigned char status = inb(base + 7);
        if (status == 0 || status == 0xFF) return -1;
        if (!(status & ATA_STATUS_BSY)) {
            return (status & (ATA_STATUS_ERR | ATA_STATUS_DF)) ? -1 : 0;
        }
    }
    return -1;
}

static int ata_wait_drq(unsigned short base, unsigned short control) {
    ata_delay_400ns(control);
    for (unsigned int timeout = 0; timeout < ATA_POLL_LIMIT; timeout++) {
        unsigned char status = inb(base + 7);
        if (status == 0 || status == 0xFF) return -1;
        if (status & (ATA_STATUS_ERR | ATA_STATUS_DF)) {
            return -1;
        }
        if (!(status & ATA_STATUS_BSY) && (status & ATA_STATUS_DRQ)) {
            return 0;
        }
    }
    return -1;
}

static unsigned char g_selected_drive;
static unsigned int g_drive_sector_count[4];

int ata_select_drive(unsigned char drive) {
    if (drive >= 4) return -1;
    g_selected_drive = drive;
    return 0;
}

int ata_probe_drive(unsigned char drive) {
    if (drive >= 4) return 0;
    g_drive_sector_count[drive] = 0;

    unsigned short base = ata_base_for_drive(drive);
    unsigned short control = ata_control_for_drive(drive);
    outb(base + 6, (unsigned char)(0xA0 | ((drive & 1) << 4)));
    ata_delay_400ns(control);
    outb(base + 2, 0);
    outb(base + 3, 0);
    outb(base + 4, 0);
    outb(base + 5, 0);
    outb(base + 7, ATA_CMD_IDENTIFY);

    unsigned char status = inb(base + 7);
    if (status == 0 || status == 0xFF) return 0;

    unsigned int timeout = ATA_POLL_LIMIT;
    while ((status = inb(base + 7)) & ATA_STATUS_BSY) {
        if (status == 0 || status == 0xFF) return 0;
        if (--timeout == 0) return 0;
    }
    if (inb(base + 4) != 0 || inb(base + 5) != 0) return 0;
    if (status & (ATA_STATUS_ERR | ATA_STATUS_DF)) return 0;
    if (!(status & ATA_STATUS_DRQ)) return 0;

    unsigned short identify[256];
    for (int i = 0; i < 256; i++) identify[i] = inw(base);
    if (identify[49] & (1 << 9)) {
        g_drive_sector_count[drive] = (unsigned int)identify[60] |
            ((unsigned int)identify[61] << 16);
        if (g_drive_sector_count[drive] > 0x10000000) {
            g_drive_sector_count[drive] = 0x10000000;
        }
    }
    return g_drive_sector_count[drive] != 0;
}

/**
 * Reads sequential sectors using LBA28 PIO Mode
 * @param lba        The absolute sector number to start reading from
 * @param count      How many sectors to read
 * @param target_buf Target RAM address pointer to copy data into
 * @return           0 on success, -1 on failure
 */
int ata_read_sectors(unsigned int lba, unsigned char count, unsigned short* target_buf) {
    unsigned short base = ata_base_for_drive(g_selected_drive);
    unsigned short control = ata_control_for_drive(g_selected_drive);
    if (!target_buf || count == 0 || lba > 0x0FFFFFFF ||
        (unsigned int)count - 1 > 0x0FFFFFFF - lba) {
        return -1;
    }

    outb(base + 6, (unsigned char)(0xE0 | ((g_selected_drive & 1) << 4) | ((lba >> 24) & 0x0F)));
    ata_delay_400ns(control);
    if (ata_wait_ready(base, control) != 0) return -1;

    outb(base + 2, count);
    outb(base + 3, (unsigned char)(lba));
    outb(base + 4, (unsigned char)(lba >> 8));
    outb(base + 5, (unsigned char)(lba >> 16));

    outb(base + 7, ATA_CMD_READ);

    // 4. Read data sector by sector (each sector contains 256 words / 512 bytes)
    for (int i = 0; i < count; i++) {
        if (ata_wait_drq(base, control) != 0) {
            return -1;
        }

        for (int j = 0; j < 256; j++) {
            target_buf[(i * 256) + j] = inw(base);
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
    unsigned short base = ata_base_for_drive(g_selected_drive);
    unsigned short control = ata_control_for_drive(g_selected_drive);
    if (!source_buf || count == 0 || lba > 0x0FFFFFFF ||
        (unsigned int)count - 1 > 0x0FFFFFFF - lba) {
        return -1;
    }

    outb(base + 6, (unsigned char)(0xE0 | ((g_selected_drive & 1) << 4) | ((lba >> 24) & 0x0F)));
    ata_delay_400ns(control);
    if (ata_wait_ready(base, control) != 0) return -1;

    outb(base + 2, count);
    outb(base + 3, (unsigned char)(lba));
    outb(base + 4, (unsigned char)(lba >> 8));
    outb(base + 5, (unsigned char)(lba >> 16));
    
    outb(base + 7, ATA_CMD_WRITE);

    // 4. Write data sector by sector
    for (int i = 0; i < count; i++) {
        if (ata_wait_drq(base, control) != 0) {
            return -1;
        }

        for (int j = 0; j < 256; j++) {
            outw(base, source_buf[(i * 256) + j]);
        }
    }

    // 5. Commit data to disk and wait until controller is ready
    outb(base + 7, ATA_CMD_CACHE_FLUSH);
    if (ata_wait_ready(base, control) != 0) return -1;

    return 0;
}

/**
 * Diagnostic Verification Test for Hard Disk I/O
 * @return 0 on success, 1 on failure
 */
int disk_test(void) {
    unsigned short first_read[256];
    unsigned short second_read[256];

    if (ata_select_drive(0) != 0 || !ata_probe_drive(0) ||
        g_drive_sector_count[0] == 0) return 1;

    unsigned int last_lba = g_drive_sector_count[0] - 1;
    unsigned int sectors[] = {0, 1, 100, 2048, last_lba / 2, last_lba};
    for (unsigned int i = 0; i < sizeof(sectors) / sizeof(sectors[0]); i++) {
        if (sectors[i] > last_lba) continue;
        int duplicate = 0;
        for (unsigned int previous = 0; previous < i; previous++) {
            if (sectors[previous] == sectors[i]) duplicate = 1;
        }
        if (duplicate) continue;
        if (ata_read_sectors(sectors[i], 1, first_read) != 0 ||
            ata_read_sectors(sectors[i], 1, second_read) != 0 ||
            memcmp(first_read, second_read, sizeof(first_read)) != 0) {
            return 1;
        }
    }
    return 0;
}
