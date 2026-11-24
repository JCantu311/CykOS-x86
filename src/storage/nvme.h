#ifndef NVME_H
#define NVME_H

int nvme_probe(unsigned int* sector_count);
int nvme_read_sectors(unsigned int lba, unsigned char count, unsigned short* buffer);
int nvme_write_sectors(unsigned int lba, unsigned char count,
                       const unsigned short* buffer);

#endif
