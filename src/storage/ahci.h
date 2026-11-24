#ifndef AHCI_H
#define AHCI_H

int ahci_probe(unsigned int* sector_count);
int ahci_read_sectors(unsigned int lba, unsigned char count, unsigned short* buffer);
int ahci_write_sectors(unsigned int lba, unsigned char count,
                       const unsigned short* buffer);

#endif
