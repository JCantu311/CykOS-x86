#ifndef CYKOS_PAGING_H
#define CYKOS_PAGING_H

typedef struct paging_space paging_space_t;

/* Enable the identity map used by the current flat physical memory layout. */
int paging_init(void);
int paging_map_identity_range(unsigned int address, unsigned int length);
int paging_map_mmio_range(unsigned int address, unsigned int length);

/* Make an identity-mapped range accessible from CPL3 (must be below 32 MiB). */
int paging_set_user_range(unsigned int address, unsigned int length, int user);
int paging_user_range_valid(unsigned int address, unsigned int length);

/* Per-process virtual address spaces (isolated low-32 MiB user mappings). */
paging_space_t* paging_create_address_space(void);
paging_space_t* paging_clone_address_space(paging_space_t* src);
paging_space_t* paging_get_active_address_space(void);
void paging_destroy_address_space(paging_space_t* space);
void paging_switch_address_space(paging_space_t* space);
int paging_alloc_user_range(paging_space_t* space, unsigned int address, unsigned int length);
int paging_free_user_range(paging_space_t* space, unsigned int address, unsigned int length);

#endif
