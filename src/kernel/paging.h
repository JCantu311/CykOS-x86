#ifndef CYKOS_PAGING_H
#define CYKOS_PAGING_H

/* Enable the identity map used by the current flat physical memory layout. */
int paging_init(void);
int paging_map_identity_range(unsigned int address, unsigned int length);
int paging_map_mmio_range(unsigned int address, unsigned int length);

/* Make an identity-mapped range accessible from CPL3 (must be below 32 MiB). */
int paging_set_user_range(unsigned int address, unsigned int length, int user);
int paging_user_range_valid(unsigned int address, unsigned int length);

#endif
