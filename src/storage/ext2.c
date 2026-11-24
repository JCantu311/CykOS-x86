#include "ext2.h"
#include "ata.h"
#include "cykastd.h"
#include "paging.h"

#define EXT2_MAGIC 0xEF53u
#define EXT2_BLOCK_SIZE 1024u
#define EXT2_ROOT_INO 2u
#define EXT2_MAX_GROUPS 256u
#define EXT2_NAME_MAX 255u
#define EXT2_MODE_DIR 0x4000u
#define LIVE_BOOT_INFO 0x04F0u
#define LIVE_BOOT_MAGIC 0x534F4B43u
#define LIVE_RAMDISK_BASE 0x02000000u
#define MULTIBOOT_DISK_INFO 0x04E0u
#define MULTIBOOT_DISK_MAGIC 0x554B5943u
#define E820_COUNT_ADDR 0x04FCu
#define E820_MAP_ADDR 0x0500u
#define E820_MAX_ENTRIES 20u

typedef struct __attribute__((packed)) {
    unsigned int base_low, base_high, length_low, length_high, type, attributes;
} ext2_e820_entry_t;

typedef struct __attribute__((packed)) {
    unsigned short mode, uid;
    unsigned int size, atime, ctime, mtime, dtime;
    unsigned short gid, links_count;
    unsigned int blocks, flags, osd1;
    unsigned int block[15];
    unsigned int generation, file_acl, dir_acl, faddr;
    unsigned char osd2[12];
} ext2_inode_t;

typedef struct __attribute__((packed)) {
    unsigned int inode;
    unsigned short rec_len;
    unsigned char name_len, file_type;
} ext2_dirent_t;

static struct {
    unsigned int partition_lba, partition_sectors, blocks_count, inodes_count, blocks_per_group;
    unsigned int inodes_per_group, inode_size, groups_count, first_data_block, first_inode;
    unsigned int inode_table[EXT2_MAX_GROUPS], block_bitmap[EXT2_MAX_GROUPS], inode_bitmap[EXT2_MAX_GROUPS];
    int sparse_super;
    int live_cd;
    int ramdisk;
    unsigned char cd_drive;
    unsigned int iso_image_lba;
    unsigned int image_blocks;
    unsigned int ramdisk_base;
    unsigned int ramdisk_bytes;
    unsigned int root_inode;
    int mounted;
    char cwd[128];
} g_ext2;

static unsigned short g_ext2_sector[256];
static unsigned short g_ext2_block[EXT2_BLOCK_SIZE / 2];
static unsigned short g_ext2_indirect[EXT2_BLOCK_SIZE / 2] __attribute__((aligned(4)));
static unsigned short g_ext2_indirect2[EXT2_BLOCK_SIZE / 2] __attribute__((aligned(4)));
static unsigned short g_ext2_dir_block[EXT2_BLOCK_SIZE / 2];
static unsigned short g_ext2_bitmap[EXT2_BLOCK_SIZE / 2];
static unsigned short g_ext2_cd_block[2048 / 2] __attribute__((aligned(4)));

int ext2_live_status(void) {
    if (!g_ext2.live_cd) return 0;
    return g_ext2.ramdisk ? 2 : 1;
}

static int ext2_memory_range_usable(unsigned int start, unsigned int end) {
    volatile unsigned int* count_ptr = (volatile unsigned int*)E820_COUNT_ADDR;
    ext2_e820_entry_t* entries = (ext2_e820_entry_t*)E820_MAP_ADDR;
    unsigned int count = *count_ptr;
    unsigned int covered = start;
    if (count > E820_MAX_ENTRIES) count = E820_MAX_ENTRIES;
    for (unsigned int pass = 0; pass < count && covered < end; pass++) {
        unsigned int previous = covered;
        for (unsigned int i = 0; i < count; i++) {
            ext2_e820_entry_t* entry = &entries[i];
            if (entry->type != 1 || !(entry->attributes & 1u) || entry->base_high != 0 ||
                entry->length_low == 0 || entry->base_low > covered) continue;
            unsigned int range_end;
            if (entry->length_high || entry->length_low > 0xFFFFFFFFu - entry->base_low)
                range_end = 0xFFFFFFFFu;
            else
                range_end = entry->base_low + entry->length_low;
            if (range_end > covered) covered = range_end;
        }
        if (covered == previous) break;
    }
    return covered >= end;
}

static int ext2_load_live_image_to_ram(void) {
    unsigned int image_bytes = g_ext2.image_blocks * 2048u;
    if (!g_ext2.image_blocks || image_bytes / 2048u != g_ext2.image_blocks ||
        LIVE_RAMDISK_BASE > 0xFFFFFFFFu - image_bytes ||
        !ext2_memory_range_usable(LIVE_RAMDISK_BASE, LIVE_RAMDISK_BASE + image_bytes)) return -1;
    if (paging_map_identity_range(LIVE_RAMDISK_BASE, image_bytes) != 0) return -1;

    unsigned int block = 0;
    while (block < g_ext2.image_blocks) {
        unsigned int left = g_ext2.image_blocks - block;
        unsigned char count = (unsigned char)(left > 16u ? 16u : left);
        unsigned int address = LIVE_RAMDISK_BASE + block * 2048u;
        if (ata_read_atapi_blocks(g_ext2.cd_drive, g_ext2.iso_image_lba + block,
                                  count, (unsigned short*)address) != 0) return -1;
        block += count;
    }
    g_ext2.ramdisk = 1;
    g_ext2.ramdisk_base = LIVE_RAMDISK_BASE;
    g_ext2.ramdisk_bytes = image_bytes;
    return 0;
}

static int ext2_walk(const char* path, unsigned int* inode_out, unsigned short* mode_out);

static unsigned short ext2_u16(const unsigned char* p) { return (unsigned short)(p[0] | ((unsigned short)p[1] << 8)); }
static unsigned int ext2_u32(const unsigned char* p) {
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

static void ext2_put_u16(unsigned char* p, unsigned int value) { p[0] = value; p[1] = value >> 8; }
static void ext2_put_u32(unsigned char* p, unsigned int value) {
    p[0] = value; p[1] = value >> 8; p[2] = value >> 16; p[3] = value >> 24;
}

static int ext2_group_has_super(unsigned int group) {
    if (group == 0 || group == 1) return 1;
    if (!g_ext2.sparse_super) return 1;
    unsigned int value = group;
    while (value % 3 == 0) value /= 3;
    if (value == 1) return 1;
    value = group;
    while (value % 5 == 0) value /= 5;
    if (value == 1) return 1;
    value = group;
    while (value % 7 == 0) value /= 7;
    return value == 1;
}

static int ext2_name_equal(const unsigned char* disk_name, unsigned int disk_len, const char* name) {
    unsigned int i = 0;
    while (i < disk_len && name[i]) {
        unsigned char a = disk_name[i], b = (unsigned char)name[i];
        if (a >= 'a' && a <= 'z') a = (unsigned char)(a - 'a' + 'A');
        if (b >= 'a' && b <= 'z') b = (unsigned char)(b - 'a' + 'A');
        if (a != b) return 0;
        i++;
    }
    return i == disk_len && name[i] == 0;
}

static int ext2_read_sectors(unsigned int lba, unsigned char count, void* out) {
    if (lba > 0xFFFFFFFFu - g_ext2.partition_lba) return -1;
    if (g_ext2.partition_sectors && (lba > g_ext2.partition_sectors || count > g_ext2.partition_sectors - lba)) return -1;
    if (g_ext2.ramdisk) {
        unsigned int image_sector = g_ext2.partition_lba + lba;
        if (image_sector > 0xFFFFFFFFu / 512u) return -1;
        unsigned int image_offset = image_sector * 512u;
        if (image_offset > g_ext2.ramdisk_bytes ||
            (unsigned int)count * 512u > g_ext2.ramdisk_bytes - image_offset)
            return -1;
        memcpy(out, (const void*)(g_ext2.ramdisk_base + image_offset), (unsigned int)count * 512u);
        return 0;
    }
    if (g_ext2.live_cd) {
        unsigned char* destination = (unsigned char*)out;
        for (unsigned int sector = 0; sector < count; sector++) {
            unsigned int image_sector = g_ext2.partition_lba + lba + sector;
            unsigned int cd_lba = g_ext2.iso_image_lba + image_sector / 4u;
            unsigned int offset = (image_sector & 3u) * 512u;
            if (ata_read_atapi_block(g_ext2.cd_drive, cd_lba, g_ext2_cd_block) != 0) return -1;
            memcpy(destination + sector * 512u, (unsigned char*)g_ext2_cd_block + offset, 512);
        }
        return 0;
    }
    if (ata_select_drive(0) != 0 || ata_read_sectors(g_ext2.partition_lba + lba, count, (unsigned short*)out) != 0) return -1;
    return 0;
}

static int ext2_read_block(unsigned int block, void* out) {
    if (block >= g_ext2.blocks_count) return -1;
    return ext2_read_sectors(block * 2u, 2, out);
}

static int ext2_write_block(unsigned int block, const void* data) {
    if ((!g_ext2.ramdisk && g_ext2.live_cd) || block >= g_ext2.blocks_count || !data || block > 0x7FFFFFFFu) return -1;
    if (g_ext2.ramdisk) {
        unsigned int image_sector = g_ext2.partition_lba + block * 2u;
        memcpy((void*)(g_ext2.ramdisk_base + image_sector * 512u), data, EXT2_BLOCK_SIZE);
        return 0;
    }
    unsigned int lba = block * 2u;
    if (lba > 0xFFFFFFFFu - g_ext2.partition_lba ||
        (g_ext2.partition_sectors && (lba > g_ext2.partition_sectors || 2u > g_ext2.partition_sectors - lba))) return -1;
    if (ata_select_drive(0) != 0 || ata_write_sectors(g_ext2.partition_lba + lba, 2, (const unsigned short*)data) != 0) return -1;
    return 0;
}

static int ext2_update_super_counts(int block_delta, int inode_delta) {
    for (unsigned int group = 0; group < g_ext2.groups_count; group++) {
        if (!ext2_group_has_super(group)) continue;
        unsigned int block = group == 0 ? 1 : g_ext2.first_data_block + group * g_ext2.blocks_per_group;
        unsigned short super_buf[EXT2_BLOCK_SIZE / 2];
        if (ext2_read_block(block, super_buf) != 0) return -1;
        unsigned char* sb = (unsigned char*)super_buf;
        if (block_delta) {
            unsigned int current = ext2_u32(sb + 12);
            if (block_delta < 0 && current < (unsigned int)(-block_delta)) return -1;
            current = block_delta > 0 ? current + (unsigned int)block_delta : current - (unsigned int)(-block_delta);
            ext2_put_u32(sb + 12, current);
        }
        if (inode_delta) {
            unsigned int current = ext2_u32(sb + 16);
            if (inode_delta < 0 && current < (unsigned int)(-inode_delta)) return -1;
            current = inode_delta > 0 ? current + (unsigned int)inode_delta : current - (unsigned int)(-inode_delta);
            ext2_put_u32(sb + 16, current);
        }
        if (ext2_write_block(block, super_buf) != 0) return -1;
    }
    return 0;
}

static int ext2_update_group_counts(unsigned int group, int block_delta, int inode_delta, int dir_delta) {
    unsigned int descriptor_index = group;
    unsigned int descriptor_block_offset = (descriptor_index * 32) / EXT2_BLOCK_SIZE;
    unsigned int descriptor_offset = (descriptor_index * 32) % EXT2_BLOCK_SIZE;
    for (unsigned int copy_group = 0; copy_group < g_ext2.groups_count; copy_group++) {
        if (!ext2_group_has_super(copy_group)) continue;
        unsigned int block = copy_group == 0
            ? 2 + descriptor_block_offset
            : g_ext2.first_data_block + copy_group * g_ext2.blocks_per_group + 1 + descriptor_block_offset;
        unsigned short desc_buf[EXT2_BLOCK_SIZE / 2];
        if (ext2_read_block(block, desc_buf) != 0) return -1;
        unsigned char* desc = (unsigned char*)desc_buf + descriptor_offset;
        if (block_delta) {
            unsigned int current = ext2_u16(desc + 12);
            if (block_delta < 0 && current < (unsigned int)(-block_delta)) return -1;
            current = block_delta > 0 ? current + (unsigned int)block_delta : current - (unsigned int)(-block_delta);
            ext2_put_u16(desc + 12, current);
        }
        if (inode_delta) {
            unsigned int current = ext2_u16(desc + 14);
            if (inode_delta < 0 && current < (unsigned int)(-inode_delta)) return -1;
            current = inode_delta > 0 ? current + (unsigned int)inode_delta : current - (unsigned int)(-inode_delta);
            ext2_put_u16(desc + 14, current);
        }
        if (dir_delta) {
            unsigned int current = ext2_u16(desc + 16);
            if (dir_delta < 0 && current < (unsigned int)(-dir_delta)) return -1;
            current = dir_delta > 0 ? current + (unsigned int)dir_delta : current - (unsigned int)(-dir_delta);
            ext2_put_u16(desc + 16, current);
        }
        if (ext2_write_block(block, desc_buf) != 0) return -1;
    }
    return 0;
}

static int ext2_alloc_block(void) {
    for (unsigned int group = 0; group < g_ext2.groups_count; group++) {
        unsigned int first = g_ext2.first_data_block + group * g_ext2.blocks_per_group;
        unsigned int count = g_ext2.blocks_per_group;
        if (first >= g_ext2.blocks_count) break;
        if (count > g_ext2.blocks_count - first) count = g_ext2.blocks_count - first;
        if (ext2_read_block(g_ext2.block_bitmap[group], g_ext2_bitmap) != 0) return -1;
        unsigned char* bits = (unsigned char*)g_ext2_bitmap;
        for (unsigned int bit = 0; bit < count; bit++) {
            if ((bits[bit >> 3] & (1u << (bit & 7))) == 0) {
                unsigned int block = first + bit;
                bits[bit >> 3] |= (unsigned char)(1u << (bit & 7));
                if (ext2_write_block(g_ext2.block_bitmap[group], g_ext2_bitmap) != 0 ||
                    ext2_update_group_counts(group, -1, 0, 0) != 0 ||
                    ext2_update_super_counts(-1, 0) != 0) return -1;
                memset(g_ext2_block, 0, EXT2_BLOCK_SIZE);
                if (ext2_write_block(block, g_ext2_block) != 0) return -1;
                return (int)block;
            }
        }
    }
    return -1;
}

static int ext2_alloc_inode(int is_dir) {
    for (unsigned int group = 0; group < g_ext2.groups_count; group++) {
        unsigned int first_inode = group * g_ext2.inodes_per_group + 1;
        unsigned int count = g_ext2.inodes_per_group;
        if (first_inode > g_ext2.inodes_count) break;
        if (count > g_ext2.inodes_count - first_inode + 1) count = g_ext2.inodes_count - first_inode + 1;
        if (ext2_read_block(g_ext2.inode_bitmap[group], g_ext2_bitmap) != 0) return -1;
        unsigned char* bits = (unsigned char*)g_ext2_bitmap;
        for (unsigned int bit = 0; bit < count; bit++) {
            unsigned int number = first_inode + bit;
            if (number < g_ext2.first_inode) continue;
            if ((bits[bit >> 3] & (1u << (bit & 7))) == 0) {
                bits[bit >> 3] |= (unsigned char)(1u << (bit & 7));
                if (ext2_write_block(g_ext2.inode_bitmap[group], g_ext2_bitmap) != 0 ||
                    ext2_update_group_counts(group, 0, -1, is_dir ? 1 : 0) != 0 ||
                    ext2_update_super_counts(0, -1) != 0) return -1;
                return (int)number;
            }
        }
    }
    return -1;
}

static int ext2_free_block(unsigned int block) {
    if (block < g_ext2.first_data_block || block >= g_ext2.blocks_count) return -1;
    unsigned int relative = block - g_ext2.first_data_block;
    unsigned int group = relative / g_ext2.blocks_per_group;
    unsigned int bit = relative % g_ext2.blocks_per_group;
    if (group >= g_ext2.groups_count || ext2_read_block(g_ext2.block_bitmap[group], g_ext2_bitmap) != 0) return -1;
    unsigned char* bits = (unsigned char*)g_ext2_bitmap;
    unsigned char mask = (unsigned char)(1u << (bit & 7));
    if (!(bits[bit >> 3] & mask)) return -1;
    bits[bit >> 3] &= (unsigned char)~mask;
    if (ext2_write_block(g_ext2.block_bitmap[group], g_ext2_bitmap) != 0 ||
        ext2_update_group_counts(group, 1, 0, 0) != 0 || ext2_update_super_counts(1, 0) != 0) return -1;
    return 0;
}

static int ext2_free_inode(unsigned int number, int is_dir) {
    if (number < g_ext2.first_inode || number > g_ext2.inodes_count) return -1;
    unsigned int index = number - 1;
    unsigned int group = index / g_ext2.inodes_per_group;
    unsigned int bit = index % g_ext2.inodes_per_group;
    if (group >= g_ext2.groups_count || ext2_read_block(g_ext2.inode_bitmap[group], g_ext2_bitmap) != 0) return -1;
    unsigned char* bits = (unsigned char*)g_ext2_bitmap;
    unsigned char mask = (unsigned char)(1u << (bit & 7));
    if (!(bits[bit >> 3] & mask)) return -1;
    bits[bit >> 3] &= (unsigned char)~mask;
    if (ext2_write_block(g_ext2.inode_bitmap[group], g_ext2_bitmap) != 0 ||
        ext2_update_group_counts(group, 0, 1, is_dir ? -1 : 0) != 0 ||
        ext2_update_super_counts(0, 1) != 0) return -1;
    return 0;
}

static int ext2_load_inode(unsigned int number, ext2_inode_t* out) {
    if (!out || number == 0 || number > g_ext2.inodes_count) return -1;
    unsigned int index = number - 1;
    unsigned int group = index / g_ext2.inodes_per_group;
    unsigned int in_group = index % g_ext2.inodes_per_group;
    if (group >= g_ext2.groups_count) return -1;
    unsigned int byte_offset = in_group * g_ext2.inode_size;
    unsigned int block = g_ext2.inode_table[group] + byte_offset / EXT2_BLOCK_SIZE;
    unsigned int offset = byte_offset % EXT2_BLOCK_SIZE;
    if (ext2_read_block(block, g_ext2_block) != 0) return -1;
    if (offset + sizeof(*out) > EXT2_BLOCK_SIZE) return -1;
    memcpy(out, (unsigned char*)g_ext2_block + offset, sizeof(*out));
    return 0;
}

static int ext2_store_inode(unsigned int number, const ext2_inode_t* inode) {
    if (!inode || number == 0 || number > g_ext2.inodes_count) return -1;
    unsigned int index = number - 1;
    unsigned int group = index / g_ext2.inodes_per_group;
    unsigned int in_group = index % g_ext2.inodes_per_group;
    unsigned int byte_offset = in_group * g_ext2.inode_size;
    unsigned int block = g_ext2.inode_table[group] + byte_offset / EXT2_BLOCK_SIZE;
    unsigned int offset = byte_offset % EXT2_BLOCK_SIZE;
    if (ext2_read_block(block, g_ext2_block) != 0 || offset + sizeof(*inode) > EXT2_BLOCK_SIZE) return -1;
    memcpy((unsigned char*)g_ext2_block + offset, inode, sizeof(*inode));
    return ext2_write_block(block, g_ext2_block);
}

static int ext2_initialize_inode(unsigned int number, const ext2_inode_t* inode) {
    if (!inode || number == 0 || number > g_ext2.inodes_count) return -1;
    unsigned int index = number - 1;
    unsigned int group = index / g_ext2.inodes_per_group;
    unsigned int in_group = index % g_ext2.inodes_per_group;
    unsigned int byte_offset = in_group * g_ext2.inode_size;
    unsigned int block = g_ext2.inode_table[group] + byte_offset / EXT2_BLOCK_SIZE;
    unsigned int offset = byte_offset % EXT2_BLOCK_SIZE;
    if (ext2_read_block(block, g_ext2_block) != 0 || offset + g_ext2.inode_size > EXT2_BLOCK_SIZE) return -1;
    memset((unsigned char*)g_ext2_block + offset, 0, g_ext2.inode_size);
    memcpy((unsigned char*)g_ext2_block + offset, inode, sizeof(*inode));
    return ext2_write_block(block, g_ext2_block);
}

static unsigned int ext2_data_block(const ext2_inode_t* inode, unsigned int logical) {
    if (logical < 12) return inode->block[logical];
    logical -= 12;
    if (logical < EXT2_BLOCK_SIZE / 4) {
        unsigned int indirect = inode->block[12];
        if (!indirect || ext2_read_block(indirect, g_ext2_indirect) != 0) return 0;
        return ((unsigned int*)g_ext2_indirect)[logical];
    }
    logical -= EXT2_BLOCK_SIZE / 4;
    if (logical < (EXT2_BLOCK_SIZE / 4) * (EXT2_BLOCK_SIZE / 4)) {
        unsigned int indirect = inode->block[13];
        if (!indirect || ext2_read_block(indirect, g_ext2_indirect) != 0) return 0;
        unsigned int second = ((unsigned int*)g_ext2_indirect)[logical / (EXT2_BLOCK_SIZE / 4)];
        if (!second || ext2_read_block(second, g_ext2_block) != 0) return 0;
        return ((unsigned int*)g_ext2_block)[logical % (EXT2_BLOCK_SIZE / 4)];
    }
    return 0;
}

static int ext2_free_inode_blocks(const ext2_inode_t* inode) {
    if (inode->block[14]) return -1;
    for (unsigned int i = 0; i < 12; i++) {
        if (inode->block[i] && ext2_free_block(inode->block[i]) != 0) return -1;
    }
    if (inode->block[12]) {
        if (ext2_read_block(inode->block[12], g_ext2_indirect) != 0) return -1;
        unsigned int* table = (unsigned int*)g_ext2_indirect;
        for (unsigned int i = 0; i < EXT2_BLOCK_SIZE / 4; i++) {
            if (table[i] && ext2_free_block(table[i]) != 0) return -1;
        }
        if (ext2_free_block(inode->block[12]) != 0) return -1;
    }
    if (inode->block[13]) {
        if (ext2_read_block(inode->block[13], g_ext2_indirect) != 0) return -1;
        unsigned int* top = (unsigned int*)g_ext2_indirect;
        for (unsigned int i = 0; i < EXT2_BLOCK_SIZE / 4; i++) {
            if (!top[i]) continue;
            if (ext2_read_block(top[i], g_ext2_indirect2) != 0) return -1;
            unsigned int* bottom = (unsigned int*)g_ext2_indirect2;
            for (unsigned int j = 0; j < EXT2_BLOCK_SIZE / 4; j++) {
                if (bottom[j] && ext2_free_block(bottom[j]) != 0) return -1;
            }
            if (ext2_free_block(top[i]) != 0) return -1;
        }
        if (ext2_free_block(inode->block[13]) != 0) return -1;
    }
    return 0;
}

static int ext2_ensure_data_block(ext2_inode_t* inode, unsigned int logical, unsigned int* physical) {
    if (logical < 12) {
        if (!inode->block[logical]) {
            int allocated = ext2_alloc_block();
            if (allocated < 0) return -1;
            inode->block[logical] = (unsigned int)allocated;
            inode->blocks += 2;
        }
        *physical = inode->block[logical];
        return 0;
    }
    logical -= 12;
    if (logical < EXT2_BLOCK_SIZE / 4) {
        if (!inode->block[12]) {
            int allocated = ext2_alloc_block();
            if (allocated < 0) return -1;
            inode->block[12] = (unsigned int)allocated;
            inode->blocks += 2;
            memset(g_ext2_indirect, 0, EXT2_BLOCK_SIZE);
        } else if (ext2_read_block(inode->block[12], g_ext2_indirect) != 0) return -1;
        unsigned int* table = (unsigned int*)g_ext2_indirect;
        if (!table[logical]) {
            int allocated = ext2_alloc_block();
            if (allocated < 0) return -1;
            table[logical] = (unsigned int)allocated;
            inode->blocks += 2;
            if (ext2_write_block(inode->block[12], g_ext2_indirect) != 0) return -1;
        }
        *physical = table[logical];
        return 0;
    }
    logical -= EXT2_BLOCK_SIZE / 4;
    if (logical >= (EXT2_BLOCK_SIZE / 4) * (EXT2_BLOCK_SIZE / 4)) return -1;
    if (!inode->block[13]) {
        int allocated = ext2_alloc_block();
        if (allocated < 0) return -1;
        inode->block[13] = (unsigned int)allocated;
        inode->blocks += 2;
        memset(g_ext2_indirect, 0, EXT2_BLOCK_SIZE);
    } else if (ext2_read_block(inode->block[13], g_ext2_indirect) != 0) return -1;
    unsigned int* top = (unsigned int*)g_ext2_indirect;
    unsigned int top_index = logical / (EXT2_BLOCK_SIZE / 4);
    unsigned int second = top[top_index];
    if (!second) {
        int allocated = ext2_alloc_block();
        if (allocated < 0) return -1;
        second = (unsigned int)allocated;
        top[top_index] = second;
        inode->blocks += 2;
        if (ext2_write_block(inode->block[13], g_ext2_indirect) != 0) return -1;
        memset(g_ext2_indirect2, 0, EXT2_BLOCK_SIZE);
    } else if (ext2_read_block(second, g_ext2_indirect2) != 0) return -1;
    unsigned int* bottom = (unsigned int*)g_ext2_indirect2;
    unsigned int bottom_index = logical % (EXT2_BLOCK_SIZE / 4);
    if (!bottom[bottom_index]) {
        int allocated = ext2_alloc_block();
        if (allocated < 0) return -1;
        bottom[bottom_index] = (unsigned int)allocated;
        inode->blocks += 2;
        if (ext2_write_block(second, g_ext2_indirect2) != 0) return -1;
    }
    *physical = bottom[bottom_index];
    return 0;
}

static int ext2_write_inode_data(ext2_inode_t* inode, const void* data, unsigned int length) {
    const unsigned char* src = (const unsigned char*)data;
    unsigned int done = 0;
    while (done < length) {
        unsigned int logical = done / EXT2_BLOCK_SIZE;
        unsigned int amount = length - done;
        if (amount > EXT2_BLOCK_SIZE) amount = EXT2_BLOCK_SIZE;
        unsigned int physical;
        if (ext2_ensure_data_block(inode, logical, &physical) != 0) return -1;
        memset(g_ext2_block, 0, EXT2_BLOCK_SIZE);
        memcpy(g_ext2_block, src + done, amount);
        if (ext2_write_block(physical, g_ext2_block) != 0) return -1;
        done += amount;
    }
    inode->size = length;
    return 0;
}

static int ext2_read_inode_at(const ext2_inode_t* inode, unsigned int offset, void* buffer, unsigned int length) {
    if (!inode || (!buffer && length)) return -1;
    if (offset >= inode->size) return 0;
    if (length > inode->size - offset) length = inode->size - offset;
    unsigned char* dst = (unsigned char*)buffer;
    unsigned int done = 0;
    while (done < length) {
        unsigned int pos = offset + done;
        unsigned int logical = pos / EXT2_BLOCK_SIZE;
        unsigned int in_block = pos % EXT2_BLOCK_SIZE;
        unsigned int amount = EXT2_BLOCK_SIZE - in_block;
        if (amount > length - done) amount = length - done;
        unsigned int physical = ext2_data_block(inode, logical);
        if (!physical) {
            /* ext2 sparse-file holes are valid logical blocks and read as zeroes. */
            memset(dst + done, 0, amount);
        } else {
            if (ext2_read_block(physical, g_ext2_block) != 0) return done ? (int)done : -1;
            memcpy(dst + done, (unsigned char*)g_ext2_block + in_block, amount);
        }
        done += amount;
    }
    return (int)done;
}

static int ext2_find_in_dir(unsigned int dir_number, const char* name, unsigned int* found, unsigned short* mode) {
    ext2_inode_t dir;
    if (ext2_load_inode(dir_number, &dir) != 0 || (dir.mode & 0xF000u) != EXT2_MODE_DIR) return -1;
    unsigned int offset = 0;
    while (offset < dir.size) {
        unsigned int logical = offset / EXT2_BLOCK_SIZE;
        unsigned int physical = ext2_data_block(&dir, logical);
        if (!physical || ext2_read_block(physical, g_ext2_dir_block) != 0) return -1;
        unsigned int in_block = offset % EXT2_BLOCK_SIZE;
        while (in_block + sizeof(ext2_dirent_t) <= EXT2_BLOCK_SIZE && offset < dir.size) {
            ext2_dirent_t* entry = (ext2_dirent_t*)((unsigned char*)g_ext2_dir_block + in_block);
            if (!entry->rec_len || entry->rec_len < sizeof(*entry) || in_block + entry->rec_len > EXT2_BLOCK_SIZE) break;
            if (entry->inode && entry->name_len &&
                ext2_name_equal((unsigned char*)entry + sizeof(*entry), entry->name_len, name)) {
                ext2_inode_t target;
                if (ext2_load_inode(entry->inode, &target) != 0) return -1;
                *found = entry->inode;
                if (mode) *mode = target.mode;
                return 0;
            }
            offset += entry->rec_len;
            in_block += entry->rec_len;
        }
        if (offset / EXT2_BLOCK_SIZE == logical) offset = (logical + 1) * EXT2_BLOCK_SIZE;
    }
    return -1;
}

static int ext2_resolve_parent(const char* path, unsigned int* parent_inode, char* leaf) {
    if (!path || !path[0] || !parent_inode || !leaf) return -1;
    unsigned int length = (unsigned int)strlen(path);
    if (length >= 256 || path[length - 1] == '/') return -1;
    unsigned int split = length;
    while (split > 0 && path[split - 1] != '/') split--;
    unsigned int leaf_len = length - split;
    if (!leaf_len || leaf_len > EXT2_NAME_MAX) return -1;
    memcpy(leaf, path + split, leaf_len);
    leaf[leaf_len] = 0;
    char parent[256];
    if (split == 0) {
        parent[0] = '.'; parent[1] = 0;
    } else if (split == 1) {
        parent[0] = '/'; parent[1] = 0;
    } else {
        unsigned int parent_len = split - 1;
        memcpy(parent, path, parent_len); parent[parent_len] = 0;
    }
    unsigned short mode;
    if (ext2_walk(parent, parent_inode, &mode) != 0 || (mode & 0xF000u) != EXT2_MODE_DIR) return -1;
    return 0;
}

static int ext2_insert_dir_entry(unsigned int dir_number, const char* name, unsigned int child_inode, unsigned char file_type) {
    ext2_inode_t dir;
    unsigned int name_len = (unsigned int)strlen(name);
    unsigned int needed = (sizeof(ext2_dirent_t) + name_len + 3u) & ~3u;
    if (name_len == 0 || name_len > EXT2_NAME_MAX || ext2_load_inode(dir_number, &dir) != 0 ||
        (dir.mode & 0xF000u) != EXT2_MODE_DIR) return -1;
    unsigned int offset = 0;
    while (offset < dir.size) {
        unsigned int logical = offset / EXT2_BLOCK_SIZE;
        unsigned int physical = ext2_data_block(&dir, logical);
        if (!physical || ext2_read_block(physical, g_ext2_dir_block) != 0) return -1;
        unsigned int in_block = offset % EXT2_BLOCK_SIZE;
        while (in_block + sizeof(ext2_dirent_t) <= EXT2_BLOCK_SIZE && offset < dir.size) {
            ext2_dirent_t* entry = (ext2_dirent_t*)((unsigned char*)g_ext2_dir_block + in_block);
            if (!entry->rec_len || entry->rec_len < sizeof(*entry) || in_block + entry->rec_len > EXT2_BLOCK_SIZE) break;
            unsigned int record_len = entry->rec_len;
            if (!entry->inode && record_len >= needed) {
                memset(entry, 0, record_len);
                entry->inode = child_inode; entry->rec_len = record_len;
                entry->name_len = (unsigned char)name_len; entry->file_type = file_type;
                memcpy((unsigned char*)entry + sizeof(*entry), name, name_len);
                return ext2_write_block(physical, g_ext2_dir_block);
            }
            unsigned int occupied = (sizeof(*entry) + entry->name_len + 3u) & ~3u;
            if (entry->inode && record_len >= occupied + needed) {
                entry->rec_len = (unsigned short)occupied;
                ext2_dirent_t* added = (ext2_dirent_t*)((unsigned char*)entry + occupied);
                memset(added, 0, record_len - occupied);
                added->inode = child_inode; added->rec_len = (unsigned short)(record_len - occupied);
                added->name_len = (unsigned char)name_len; added->file_type = file_type;
                memcpy((unsigned char*)added + sizeof(*added), name, name_len);
                return ext2_write_block(physical, g_ext2_dir_block);
            }
            offset += record_len;
            in_block += record_len;
        }
        if (offset / EXT2_BLOCK_SIZE == logical) offset = (logical + 1) * EXT2_BLOCK_SIZE;
    }
    return -1;
}

static int ext2_remove_dir_entry(unsigned int dir_number, const char* name, unsigned int expected_inode) {
    ext2_inode_t dir;
    if (ext2_load_inode(dir_number, &dir) != 0 || (dir.mode & 0xF000u) != EXT2_MODE_DIR) return -1;
    unsigned int offset = 0;
    while (offset < dir.size) {
        unsigned int logical = offset / EXT2_BLOCK_SIZE;
        unsigned int physical = ext2_data_block(&dir, logical);
        if (!physical || ext2_read_block(physical, g_ext2_dir_block) != 0) return -1;
        unsigned int in_block = offset % EXT2_BLOCK_SIZE;
        while (in_block + sizeof(ext2_dirent_t) <= EXT2_BLOCK_SIZE && offset < dir.size) {
            ext2_dirent_t* entry = (ext2_dirent_t*)((unsigned char*)g_ext2_dir_block + in_block);
            if (!entry->rec_len || entry->rec_len < sizeof(*entry) || in_block + entry->rec_len > EXT2_BLOCK_SIZE) break;
            if (entry->inode == expected_inode && entry->name_len &&
                ext2_name_equal((unsigned char*)entry + sizeof(*entry), entry->name_len, name)) {
                entry->inode = 0;
                entry->file_type = 0;
                return ext2_write_block(physical, g_ext2_dir_block);
            }
            offset += entry->rec_len; in_block += entry->rec_len;
        }
        if (offset / EXT2_BLOCK_SIZE == logical) offset = (logical + 1) * EXT2_BLOCK_SIZE;
    }
    return -1;
}

static int ext2_directory_is_empty(const ext2_inode_t* dir) {
    unsigned int offset = 0;
    while (offset < dir->size) {
        unsigned int logical = offset / EXT2_BLOCK_SIZE;
        unsigned int physical = ext2_data_block(dir, logical);
        if (!physical || ext2_read_block(physical, g_ext2_dir_block) != 0) return 0;
        unsigned int in_block = offset % EXT2_BLOCK_SIZE;
        while (in_block + sizeof(ext2_dirent_t) <= EXT2_BLOCK_SIZE && offset < dir->size) {
            ext2_dirent_t* entry = (ext2_dirent_t*)((unsigned char*)g_ext2_dir_block + in_block);
            if (!entry->rec_len || entry->rec_len < sizeof(*entry) || in_block + entry->rec_len > EXT2_BLOCK_SIZE) break;
            if (entry->inode && entry->name_len &&
                !ext2_name_equal((unsigned char*)entry + sizeof(*entry), entry->name_len, ".") &&
                !ext2_name_equal((unsigned char*)entry + sizeof(*entry), entry->name_len, "..")) return 0;
            offset += entry->rec_len; in_block += entry->rec_len;
        }
        if (offset / EXT2_BLOCK_SIZE == logical) offset = (logical + 1) * EXT2_BLOCK_SIZE;
    }
    return 1;
}

static int ext2_delete_file(const char* path) {
    unsigned int parent, number; unsigned short mode;
    char leaf[EXT2_NAME_MAX + 1];
    if (ext2_resolve_parent(path, &parent, leaf) != 0 || ext2_find_in_dir(parent, leaf, &number, &mode) != 0 ||
        (mode & 0xF000u) == EXT2_MODE_DIR) return -1;
    ext2_inode_t inode, empty;
    if (ext2_load_inode(number, &inode) != 0 || ext2_remove_dir_entry(parent, leaf, number) != 0 ||
        ext2_free_inode_blocks(&inode) != 0) return -1;
    memset(&empty, 0, sizeof(empty));
    return ext2_store_inode(number, &empty) == 0 ? ext2_free_inode(number, 0) : -1;
}

static int ext2_mkdir(const char* path) {
    unsigned int parent, existing; unsigned short mode;
    char leaf[EXT2_NAME_MAX + 1];
    if (ext2_resolve_parent(path, &parent, leaf) != 0 || ext2_find_in_dir(parent, leaf, &existing, &mode) == 0) return -1;
    int inode_number = ext2_alloc_inode(1);
    if (inode_number < 0) return -1;
    int block = ext2_alloc_block();
    if (block < 0) return -1;
    ext2_inode_t child;
    memset(&child, 0, sizeof(child));
    child.mode = 0040755;
    child.links_count = 2;
    child.size = EXT2_BLOCK_SIZE;
    child.blocks = 2;
    child.block[0] = (unsigned int)block;
    memset(g_ext2_dir_block, 0, EXT2_BLOCK_SIZE);
    ext2_dirent_t* dot = (ext2_dirent_t*)g_ext2_dir_block;
    dot->inode = (unsigned int)inode_number; dot->rec_len = 12; dot->name_len = 1; dot->file_type = 2;
    memcpy((unsigned char*)dot + sizeof(*dot), ".", 1);
    ext2_dirent_t* dotdot = (ext2_dirent_t*)((unsigned char*)g_ext2_dir_block + 12);
    dotdot->inode = parent; dotdot->rec_len = EXT2_BLOCK_SIZE - 12; dotdot->name_len = 2; dotdot->file_type = 2;
    memcpy((unsigned char*)dotdot + sizeof(*dotdot), "..", 2);
    ext2_inode_t parent_inode;
    if (ext2_write_block((unsigned int)block, g_ext2_dir_block) != 0 ||
        ext2_initialize_inode((unsigned int)inode_number, &child) != 0 ||
        ext2_insert_dir_entry(parent, leaf, (unsigned int)inode_number, 2) != 0 ||
        ext2_load_inode(parent, &parent_inode) != 0) return -1;
    parent_inode.links_count++;
    return ext2_store_inode(parent, &parent_inode);
}

static int ext2_rmdir(const char* path) {
    unsigned int parent, number; unsigned short mode;
    char leaf[EXT2_NAME_MAX + 1];
    if (ext2_resolve_parent(path, &parent, leaf) != 0 || ext2_find_in_dir(parent, leaf, &number, &mode) != 0 ||
        (mode & 0xF000u) != EXT2_MODE_DIR || number == EXT2_ROOT_INO) return -1;
    ext2_inode_t dir, parent_inode, empty;
    if (ext2_load_inode(number, &dir) != 0 || !ext2_directory_is_empty(&dir) ||
        ext2_remove_dir_entry(parent, leaf, number) != 0 || ext2_free_inode_blocks(&dir) != 0 ||
        ext2_load_inode(parent, &parent_inode) != 0) return -1;
    memset(&empty, 0, sizeof(empty));
    if (ext2_store_inode(number, &empty) != 0 || ext2_free_inode(number, 1) != 0) return -1;
    if (parent_inode.links_count) parent_inode.links_count--;
    return ext2_store_inode(parent, &parent_inode);
}

static int ext2_write_file(const char* path, const void* data, unsigned int length) {
    if (!g_ext2.mounted || (!data && length)) return -1;
    unsigned int parent, number;
    unsigned short mode;
    char leaf[EXT2_NAME_MAX + 1];
    if (ext2_resolve_parent(path, &parent, leaf) != 0) return -1;
    if (ext2_find_in_dir(parent, leaf, &number, &mode) == 0) {
        if ((mode & 0xF000u) == EXT2_MODE_DIR) return -1;
        ext2_inode_t inode;
        if (ext2_load_inode(number, &inode) != 0 || ext2_write_inode_data(&inode, data, length) != 0 ||
            ext2_store_inode(number, &inode) != 0) return -1;
        return 0;
    }
    int allocated_inode = ext2_alloc_inode(0);
    if (allocated_inode < 0) return -1;
    number = (unsigned int)allocated_inode;
    ext2_inode_t inode;
    memset(&inode, 0, sizeof(inode));
    inode.mode = 0100644;
    inode.links_count = 1;
    if (ext2_write_inode_data(&inode, data, length) != 0 || ext2_initialize_inode(number, &inode) != 0 ||
        ext2_insert_dir_entry(parent, leaf, number, 1) != 0) return -1;
    return 0;
}

static int ext2_walk(const char* path, unsigned int* inode_out, unsigned short* mode_out) {
    if (!g_ext2.mounted || !path || !inode_out) return -1;
    char full[256];
    unsigned int n = 0;
    if (path[0] == '/') {
        while (path[n] && n < sizeof(full) - 1) { full[n] = path[n]; n++; }
    } else {
        unsigned int cwd_len = (unsigned int)strlen(g_ext2.cwd);
        while (n < cwd_len && n < sizeof(full) - 1) { full[n] = g_ext2.cwd[n]; n++; }
        if (n > 1 && n < sizeof(full) - 1) full[n++] = '/';
        unsigned int p = 0;
        while (path[p] && n < sizeof(full) - 1) full[n++] = path[p++];
    }
    full[n] = 0;
    unsigned int current = EXT2_ROOT_INO;
    unsigned short current_mode = EXT2_MODE_DIR;
    unsigned int pos = 0;
    while (pos < n) {
        while (pos < n && full[pos] == '/') pos++;
        if (pos >= n) break;
        char component[EXT2_NAME_MAX + 1]; unsigned int len = 0;
        while (pos < n && full[pos] != '/') {
            if (len >= EXT2_NAME_MAX) return -1;
            component[len++] = full[pos++];
        }
        component[len] = 0;
        if (len == 1 && component[0] == '.') continue;
        unsigned int next; unsigned short next_mode;
        if (ext2_find_in_dir(current, component, &next, &next_mode) != 0) return -1;
        current = next; current_mode = next_mode;
    }
    *inode_out = current;
    if (mode_out) *mode_out = current_mode;
    return 0;
}

static int ext2_mount_at(unsigned int partition_lba, unsigned int partition_sectors) {
    unsigned char sb[1024];
    int live_cd = g_ext2.live_cd;
    int ramdisk = g_ext2.ramdisk;
    unsigned char cd_drive = g_ext2.cd_drive;
    unsigned int iso_image_lba = g_ext2.iso_image_lba;
    unsigned int image_blocks = g_ext2.image_blocks;
    unsigned int ramdisk_base = g_ext2.ramdisk_base;
    unsigned int ramdisk_bytes = g_ext2.ramdisk_bytes;
    memset(&g_ext2, 0, sizeof(g_ext2));
    g_ext2.live_cd = live_cd;
    g_ext2.ramdisk = ramdisk;
    g_ext2.cd_drive = cd_drive;
    g_ext2.iso_image_lba = iso_image_lba;
    g_ext2.image_blocks = image_blocks;
    g_ext2.ramdisk_base = ramdisk_base;
    g_ext2.ramdisk_bytes = ramdisk_bytes;
    g_ext2.partition_lba = partition_lba;
    g_ext2.partition_sectors = partition_sectors;
    if (ext2_read_sectors(2, 2, sb) != 0 || ext2_u16(sb + 56) != EXT2_MAGIC) return -1;
    unsigned int log_block_size = ext2_u32(sb + 24);
    unsigned int rev = ext2_u32(sb + 76);
    unsigned int incompat = rev ? ext2_u32(sb + 96) : 0;
    unsigned int ro_compat = rev ? ext2_u32(sb + 100) : 0;
    if (log_block_size != 0 || (incompat & ~0x2u) || (ro_compat & ~0x7u)) return -1;
    g_ext2.sparse_super = (ro_compat & 0x1u) != 0;
    g_ext2.inodes_count = ext2_u32(sb + 0);
    g_ext2.blocks_count = ext2_u32(sb + 4);
    g_ext2.first_data_block = ext2_u32(sb + 20);
    g_ext2.blocks_per_group = ext2_u32(sb + 32);
    g_ext2.inodes_per_group = ext2_u32(sb + 40);
    g_ext2.first_inode = rev ? ext2_u32(sb + 84) : 11;
    g_ext2.inode_size = rev ? ext2_u16(sb + 88) : 128;
    if (!g_ext2.inode_size) g_ext2.inode_size = 128;
    if ((g_ext2.inode_size != 128 && g_ext2.inode_size != 256) || !g_ext2.inodes_count || !g_ext2.blocks_count ||
        !g_ext2.blocks_per_group || g_ext2.blocks_per_group > EXT2_BLOCK_SIZE * 8u ||
        !g_ext2.inodes_per_group || g_ext2.inodes_per_group > EXT2_BLOCK_SIZE * 8u ||
        (partition_sectors && g_ext2.blocks_count > partition_sectors / 2u)) return -1;
    g_ext2.groups_count = (g_ext2.blocks_count + g_ext2.blocks_per_group - 1) / g_ext2.blocks_per_group;
    if (!g_ext2.groups_count || g_ext2.groups_count > EXT2_MAX_GROUPS) return -1;
    for (unsigned int i = 0; i < g_ext2.groups_count; i++) {
        unsigned int desc_block = 2 + (i * 32) / EXT2_BLOCK_SIZE;
        unsigned int desc_offset = (i * 32) % EXT2_BLOCK_SIZE;
        if (ext2_read_block(desc_block, g_ext2_block) != 0) return -1;
        unsigned char* desc = (unsigned char*)g_ext2_block + desc_offset;
        g_ext2.block_bitmap[i] = ext2_u32(desc + 0);
        g_ext2.inode_bitmap[i] = ext2_u32(desc + 4);
        g_ext2.inode_table[i] = ext2_u32(desc + 8);
        if (!g_ext2.block_bitmap[i] || !g_ext2.inode_bitmap[i] || !g_ext2.inode_table[i] ||
            g_ext2.block_bitmap[i] >= g_ext2.blocks_count || g_ext2.inode_bitmap[i] >= g_ext2.blocks_count ||
            g_ext2.inode_table[i] >= g_ext2.blocks_count) return -1;
    }
    g_ext2.root_inode = EXT2_ROOT_INO;
    g_ext2.mounted = 1;
    memcpy(g_ext2.cwd, "/", 2);
    ext2_inode_t root;
    if (ext2_load_inode(g_ext2.root_inode, &root) != 0 || (root.mode & 0xF000u) != EXT2_MODE_DIR) {
        g_ext2.mounted = 0; return -1;
    }
    return 0;
}

static int ext2_mount_root(void) {
    volatile unsigned int* multiboot_disk = (volatile unsigned int*)MULTIBOOT_DISK_INFO;
    if (multiboot_disk[0] == MULTIBOOT_DISK_MAGIC) {
        unsigned int source = multiboot_disk[1];
        unsigned int bytes = multiboot_disk[2];
        if (!source || bytes < 512u || source > 0xFFFFFFFFu - bytes ||
            bytes > 0xFFFFFFFFu - LIVE_RAMDISK_BASE ||
            !ext2_memory_range_usable(LIVE_RAMDISK_BASE, LIVE_RAMDISK_BASE + bytes) ||
            paging_map_identity_range(source, bytes) != 0 ||
            paging_map_identity_range(LIVE_RAMDISK_BASE, bytes) != 0) return -1;
        /* Reserve the fixed live-RAM range above app heaps and copy the
         * Multiboot module there before kernel allocations can reuse it. */
        memmove((void*)LIVE_RAMDISK_BASE, (const void*)source, bytes);
        g_ext2.live_cd = 1;
        g_ext2.ramdisk = 1;
        g_ext2.ramdisk_base = LIVE_RAMDISK_BASE;
        g_ext2.ramdisk_bytes = bytes;
        if (ext2_read_sectors(0, 1, g_ext2_sector) != 0) return -1;
        unsigned char* mbr = (unsigned char*)g_ext2_sector;
        if (mbr[510] != 0x55 || mbr[511] != 0xAA) return -1;
        for (unsigned int i = 0; i < 4; ++i) {
            unsigned char* part = mbr + 446 + i * 16;
            unsigned int start = ext2_u32(part + 8);
            unsigned int sectors = ext2_u32(part + 12);
            if (part[4] == 0x83 && start && sectors && ext2_mount_at(start, sectors) == 0)
                return 0;
        }
        return -1;
    }

    volatile unsigned int* live_info = (volatile unsigned int*)LIVE_BOOT_INFO;
    if (live_info[0] == LIVE_BOOT_MAGIC && live_info[1] != 0) {
        g_ext2.live_cd = 1;
        g_ext2.iso_image_lba = live_info[1];
        g_ext2.image_blocks = live_info[2];
        if (!g_ext2.image_blocks || g_ext2.image_blocks > 0x00100000u) return -1;
        unsigned char cd_drive;
        for (cd_drive = 0; cd_drive < 4; cd_drive++) {
            if (ata_probe_atapi_drive(cd_drive)) break;
        }
        if (cd_drive >= 4) return -1;
        g_ext2.cd_drive = cd_drive;
        (void)ext2_load_live_image_to_ram();
        if (ext2_read_sectors(0, 1, g_ext2_sector) != 0) return -1;
        unsigned char* live_mbr = (unsigned char*)g_ext2_sector;
        if (live_mbr[510] != 0x55 || live_mbr[511] != 0xAA) return -1;
        for (unsigned int i = 0; i < 4; i++) {
            unsigned char* part = live_mbr + 446 + i * 16;
            unsigned int start = ext2_u32(part + 8);
            unsigned int sectors = ext2_u32(part + 12);
            if (part[4] == 0x83 && start && sectors && ext2_mount_at(start, sectors) == 0) return 0;
        }
        return -1;
    }
    if (!ata_probe_drive(0) || ata_select_drive(0) != 0 || ata_read_sectors(0, 1, g_ext2_sector) != 0) return -1;
    unsigned char* mbr = (unsigned char*)g_ext2_sector;
    if (mbr[510] == 0x55 && mbr[511] == 0xAA) {
        for (unsigned int i = 0; i < 4; i++) {
            unsigned char* part = mbr + 446 + i * 16;
            unsigned int start = ext2_u32(part + 8);
            unsigned int sectors = ext2_u32(part + 12);
            if (part[4] == 0x83 && start && sectors && ext2_mount_at(start, sectors) == 0) return 0;
        }
    }
    return ext2_mount_at(0, 0);
}

static int ext2_find_file(const char* path, vfs_file_t* out) {
    unsigned int number; unsigned short mode; ext2_inode_t inode;
    if (!out || ext2_walk(path, &number, &mode) != 0 || ext2_load_inode(number, &inode) != 0) return -1;
    memset(out, 0, sizeof(*out));
    out->size = inode.size; out->first_cluster = number;
    out->attributes = (mode & 0xF000u) == EXT2_MODE_DIR ? 0x10 : 0;
    return 0;
}

static int ext2_read_at(const vfs_file_t* file, unsigned int offset, void* buffer, unsigned int length) {
    ext2_inode_t inode;
    if (!file || ext2_load_inode(file->first_cluster, &inode) != 0) return -1;
    return ext2_read_inode_at(&inode, offset, buffer, length);
}

static int ext2_list(const char* path, vfs_dir_callback_t callback) {
    unsigned int number; unsigned short mode; ext2_inode_t dir;
    if (!callback || ext2_walk(path && path[0] ? path : ".", &number, &mode) != 0 ||
        (mode & 0xF000u) != EXT2_MODE_DIR || ext2_load_inode(number, &dir) != 0) return -1;
    unsigned int offset = 0;
    while (offset < dir.size) {
        unsigned int logical = offset / EXT2_BLOCK_SIZE;
        unsigned int physical = ext2_data_block(&dir, logical);
        if (!physical || ext2_read_block(physical, g_ext2_dir_block) != 0) return -1;
        unsigned int in_block = offset % EXT2_BLOCK_SIZE;
        while (in_block + sizeof(ext2_dirent_t) <= EXT2_BLOCK_SIZE && offset < dir.size) {
            ext2_dirent_t* entry = (ext2_dirent_t*)((unsigned char*)g_ext2_dir_block + in_block);
            if (!entry->rec_len || entry->rec_len < sizeof(*entry) || in_block + entry->rec_len > EXT2_BLOCK_SIZE) break;
            if (entry->inode && entry->name_len) {
                char name[EXT2_NAME_MAX + 1]; memcpy(name, (unsigned char*)entry + sizeof(*entry), entry->name_len); name[entry->name_len] = 0;
                ext2_inode_t child;
                if (ext2_load_inode(entry->inode, &child) != 0) return -1;
                callback(name, child.size, (child.mode & 0xF000u) == EXT2_MODE_DIR);
            }
            offset += entry->rec_len; in_block += entry->rec_len;
        }
        if (offset / EXT2_BLOCK_SIZE == logical) offset = (logical + 1) * EXT2_BLOCK_SIZE;
    }
    return 0;
}

static int ext2_chdir(const char* path) {
    unsigned int inode; unsigned short mode;
    if (ext2_walk(path, &inode, &mode) != 0 || (mode & 0xF000u) != EXT2_MODE_DIR) return -1;
    char combined[256];
    unsigned int input_len = 0;
    if (path[0] != '/') {
        unsigned int cwd_len = (unsigned int)strlen(g_ext2.cwd);
        if (cwd_len >= sizeof(combined) - 1) return -1;
        memcpy(combined, g_ext2.cwd, cwd_len);
        input_len = cwd_len;
        if (input_len > 1) combined[input_len++] = '/';
    }
    unsigned int path_index = 0;
    while (path[path_index]) {
        if (input_len >= sizeof(combined) - 1) return -1;
        combined[input_len++] = path[path_index++];
    }
    combined[input_len] = 0;

    char normalized[128];
    unsigned int output_len = 1;
    normalized[0] = '/';
    unsigned int pos = 0;
    while (pos < input_len) {
        while (pos < input_len && combined[pos] == '/') pos++;
        if (pos == input_len) break;
        unsigned int component_start = pos;
        while (pos < input_len && combined[pos] != '/') pos++;
        unsigned int component_len = pos - component_start;
        if (component_len == 1 && combined[component_start] == '.') continue;
        if (component_len == 2 && combined[component_start] == '.' && combined[component_start + 1] == '.') {
            while (output_len > 1 && normalized[output_len - 1] != '/') output_len--;
            if (output_len > 1) output_len--;
            continue;
        }
        unsigned int separator = output_len > 1 ? 1 : 0;
        if (output_len + separator + component_len >= sizeof(normalized)) return -1;
        if (separator) normalized[output_len++] = '/';
        memcpy(normalized + output_len, combined + component_start, component_len);
        output_len += component_len;
    }
    normalized[output_len] = 0;
    memcpy(g_ext2.cwd, normalized, output_len + 1);
    return 0;
}

static int ext2_getcwd(char* buffer, unsigned int capacity) {
    unsigned int n=(unsigned int)strlen(g_ext2.cwd); if (!buffer || capacity <= n) return -1; memcpy(buffer,g_ext2.cwd,n+1); return (int)n;
}

const vfs_driver_t ext2_vfs_driver = {
    .name = "ext2",
    .mount_root = ext2_mount_root,
    .find_file = ext2_find_file,
    .read_at = ext2_read_at,
    .write_file = ext2_write_file,
    .delete_file = ext2_delete_file,
    .list_directory = ext2_list,
    .mkdir = ext2_mkdir,
    .rmdir = ext2_rmdir,
    .chdir = ext2_chdir,
    .getcwd = ext2_getcwd,
};
