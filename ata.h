#ifndef ATA_H
#define ATA_H

// Core ATA PIO LBA28 Function Declarations
int ata_read_sectors(unsigned int lba, unsigned char count, unsigned short* target_buf);
int ata_write_sectors(unsigned int lba, unsigned char count, const unsigned short* source_buf);
int ata_select_drive(unsigned char drive);
int ata_probe_drive(unsigned char drive);
int disk_test(void);

#endif // ATA_H
