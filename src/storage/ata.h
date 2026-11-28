#ifndef ATA_H
#define ATA_H

// Core ATA PIO LBA28 Function Declarations
int ata_read_sectors(unsigned int lba, unsigned char count, unsigned short* target_buf);
int ata_write_sectors(unsigned int lba, unsigned char count, const unsigned short* source_buf);
void ata_set_defer_flush(int defer);
int ata_flush_cache(void);
int ata_select_drive(unsigned char drive);
int ata_probe_drive(unsigned char drive);
int ata_probe_atapi_drive(unsigned char drive);
int ata_read_atapi_block(unsigned char drive, unsigned int lba, unsigned short* target_buf);
int ata_read_atapi_blocks(unsigned char drive, unsigned int lba, unsigned char count, unsigned short* target_buf);
int disk_test(void);

#endif // ATA_H
