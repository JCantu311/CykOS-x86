#ifndef FAT32_H
#define FAT32_H

#include "vfs.h"

int fat32_init(void);
extern const vfs_driver_t fat32_vfs_driver;

#endif