#ifndef EXT2_H
#define EXT2_H

#include "vfs.h"

/* Initial ext2 backend: 1 KiB blocks and direct/indirect file data. */
extern const vfs_driver_t ext2_vfs_driver;
/* 0: normal disk boot, 1: read-only CD root, 2: writable RAM-backed live root. */
int ext2_live_status(void);

#endif
