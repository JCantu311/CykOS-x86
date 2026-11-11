#include "fat32.h"
#include "ata.h"
#include "cykastd.h"

#define FAT32_EOC 0x0FFFFFF8
#define FAT32_MASK 0x0FFFFFFF
#define FAT32_MAX_PATH 256

typedef struct __attribute__((packed)) {
    unsigned char jump[3];
    char oem[8];
    unsigned short bytes_per_sector;
    unsigned char sectors_per_cluster;
    unsigned short reserved_sectors;
    unsigned char fat_count;
    unsigned short root_entries;
    unsigned short total_sectors_16;
    unsigned char media;
    unsigned short fat16_sectors;
    unsigned short sectors_per_track;
    unsigned short heads;
    unsigned int hidden_sectors;
    unsigned int total_sectors_32;
    unsigned int fat32_sectors;
    unsigned short flags;
    unsigned short version;
    unsigned int root_cluster;
    unsigned short fsinfo_sector;
    unsigned short backup_sector;
    unsigned char reserved[12];
    unsigned char drive_number;
    unsigned char reserved2;
    unsigned char boot_signature;
    unsigned int volume_id;
    char volume_label[11];
    char fs_type[8];
} fat32_bpb_t;

typedef struct __attribute__((packed)) {
    unsigned char name[8];
    unsigned char extension[3];
    unsigned char attributes;
    unsigned char reserved;
    unsigned char create_tenth;
    unsigned short create_time;
    unsigned short create_date;
    unsigned short access_date;
    unsigned short first_cluster_high;
    unsigned short write_time;
    unsigned short write_date;
    unsigned short first_cluster_low;
    unsigned int size;
} fat32_raw_entry_t;

typedef struct __attribute__((packed)) {
    unsigned char status;
    unsigned char chs_start[3];
    unsigned char type;
    unsigned char chs_end[3];
    unsigned int lba_start;
    unsigned int sectors;
} fat32_partition_t;

typedef struct {
    unsigned char drive;
    unsigned int partition_lba;
    unsigned int partition_sectors;
    unsigned int fat_lba;
    unsigned int data_lba;
    unsigned int total_clusters;
    unsigned int root_cluster;
    unsigned int cwd_cluster;
    unsigned int cached_fat_lba;
    unsigned short bytes_per_sector;
    unsigned short reserved_sectors;
    unsigned short sectors_per_fat;
    unsigned char sectors_per_cluster;
    unsigned char fat_count;
    int mounted;
    char cwd[FAT32_MAX_PATH];
} fat32_volume_t;

static fat32_volume_t g_fat32;
static unsigned short g_sector[256];
static unsigned short g_fat_sector[256];

static int fat32_mount_at(unsigned char drive, unsigned int lba, unsigned int partition_sectors) {
    fat32_bpb_t* bpb;
    if (ata_select_drive(drive) != 0 || ata_read_sectors(lba, 1, g_sector) != 0) return -1;
    bpb = (fat32_bpb_t*)g_sector;

    if (bpb->bytes_per_sector != 512 || bpb->sectors_per_cluster == 0 ||
        (bpb->sectors_per_cluster & (bpb->sectors_per_cluster - 1)) != 0 ||
        bpb->reserved_sectors == 0 || bpb->fat_count == 0 || bpb->fat32_sectors == 0 ||
        bpb->fat16_sectors != 0 || bpb->root_entries != 0 || bpb->root_cluster < 2) return -1;

    unsigned int total_sectors = bpb->total_sectors_16 ? bpb->total_sectors_16 : bpb->total_sectors_32;
    unsigned int data_start = bpb->reserved_sectors + (unsigned int)bpb->fat_count * bpb->fat32_sectors;
    if (total_sectors <= data_start || (partition_sectors && total_sectors > partition_sectors)) return -1;

    unsigned int clusters = (total_sectors - data_start) / bpb->sectors_per_cluster;
    if (clusters == 0 || clusters > 0x0FFFFFF5 || bpb->root_cluster >= clusters + 2) return -1;

    memset(&g_fat32, 0, sizeof(g_fat32));
    g_fat32.drive = drive;
    g_fat32.partition_lba = lba;
    g_fat32.partition_sectors = partition_sectors;
    g_fat32.fat_lba = lba + bpb->reserved_sectors;
    g_fat32.data_lba = lba + data_start;
    g_fat32.total_clusters = clusters;
    g_fat32.root_cluster = bpb->root_cluster & FAT32_MASK;
    g_fat32.cwd_cluster = g_fat32.root_cluster;
    g_fat32.cached_fat_lba = 0xFFFFFFFF;
    g_fat32.bytes_per_sector = bpb->bytes_per_sector;
    g_fat32.reserved_sectors = bpb->reserved_sectors;
    g_fat32.sectors_per_fat = bpb->fat32_sectors;
    g_fat32.sectors_per_cluster = bpb->sectors_per_cluster;
    g_fat32.fat_count = bpb->fat_count;
    g_fat32.mounted = 1;
    memcpy(g_fat32.cwd, "/", 2);
    return 0;
}

static unsigned int fat32_next_cluster(unsigned int cluster) {
    unsigned int byte_offset = cluster * 4;
    unsigned int lba = g_fat32.fat_lba + byte_offset / g_fat32.bytes_per_sector;
    unsigned int index = (byte_offset % g_fat32.bytes_per_sector) / 2;
    if (cluster >= g_fat32.total_clusters + 2) return FAT32_EOC;
    if (g_fat32.cached_fat_lba != lba) {
        if (ata_select_drive(g_fat32.drive) != 0 || ata_read_sectors(lba, 1, g_fat_sector) != 0) {
            return FAT32_EOC;
        }
        g_fat32.cached_fat_lba = lba;
    }
    unsigned int value = ((unsigned int*)g_fat_sector)[index / 2] & FAT32_MASK;
    return value;
}

static void fat32_format_name(const unsigned char* raw_name, const unsigned char* raw_ext, char* output) {
    unsigned int pos = 0;
    for (unsigned int i = 0; i < 8 && raw_name[i] != ' '; i++) output[pos++] = (char)raw_name[i];
    if (raw_ext[0] != ' ') {
        output[pos++] = '.';
        for (unsigned int i = 0; i < 3 && raw_ext[i] != ' '; i++) output[pos++] = (char)raw_ext[i];
    }
    output[pos] = '\0';
}

static int fat32_name_equal(const char* left, const char* right) {
    while (*left && *right) {
        char a = *left++, b = *right++;
        if (a >= 'a' && a <= 'z') a -= 'a' - 'A';
        if (b >= 'a' && b <= 'z') b -= 'a' - 'A';
        if (a != b) return 0;
    }
    return *left == *right;
}

static void fat32_format_83(const char* name, unsigned char base[8], unsigned char ext[3]) {
    memset(base, ' ', 8);
    memset(ext, ' ', 3);
    if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) {
        base[0] = '.';
        if (name[1] == '.') base[1] = '.';
        return;
    }
    unsigned int i = 0;
    while (name[i] && name[i] != '.' && i < 8) {
        char ch = name[i];
        if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
        base[i++] = (unsigned char)ch;
    }
    if (name[i] == '.') {
        i++;
        for (unsigned int j = 0; j < 3 && name[i]; j++, i++) {
            char ch = name[i];
            if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
            ext[j] = (unsigned char)ch;
        }
    }
}

static int fat32_find_in_dir(unsigned int start_cluster, const char* name, vfs_file_t* output,
    unsigned int* out_lba, unsigned int* out_slot) {
    unsigned char target[8], extension[3];
    fat32_format_83(name, target, extension);
    unsigned int cluster = start_cluster;
    unsigned int traversed = 0;
    while (cluster >= 2 && cluster < FAT32_EOC && traversed++ < g_fat32.total_clusters) {
        unsigned int base_lba = g_fat32.data_lba + (cluster - 2) * g_fat32.sectors_per_cluster;
        for (unsigned int sector = 0; sector < g_fat32.sectors_per_cluster; sector++) {
            if (ata_select_drive(g_fat32.drive) != 0 || ata_read_sectors(base_lba + sector, 1, g_sector) != 0) return -1;
            fat32_raw_entry_t* entries = (fat32_raw_entry_t*)g_sector;
            for (unsigned int i = 0; i < 16; i++) {
                fat32_raw_entry_t* entry = &entries[i];
                if (entry->name[0] == 0) return -1;
                if (entry->name[0] == 0xE5 || entry->attributes == 0x0F || (entry->attributes & 0x08)) continue;
                if (memcmp(entry->name, target, 8) != 0 || memcmp(entry->extension, extension, 3) != 0) continue;
                if (output) {
                    fat32_format_name(entry->name, entry->extension, output->name);
                    output->size = entry->size;
                    output->first_cluster = (((unsigned int)entry->first_cluster_high << 16) | entry->first_cluster_low) & FAT32_MASK;
                    output->attributes = entry->attributes;
                    output->volume_index = 0;
                }
                if (out_lba) *out_lba = base_lba + sector;
                if (out_slot) *out_slot = i;
                return 0;
            }
        }
        cluster = fat32_next_cluster(cluster);
    }
    return -1;
}

static int fat32_traverse(const char* path, unsigned int* out_cluster) {
    unsigned int cluster = g_fat32.cwd_cluster;
    const char* cursor = path;
    if (!path || !g_fat32.mounted) return -1;
    if (*cursor == '/' || *cursor == '\\') {
        cluster = g_fat32.root_cluster;
        while (*cursor == '/' || *cursor == '\\') cursor++;
    }
    while (*cursor) {
        char segment[13];
        unsigned int length = 0;
        while (*cursor && *cursor != '/' && *cursor != '\\') {
            if (length >= sizeof(segment) - 1) return -1;
            segment[length++] = *cursor++;
        }
        segment[length] = '\0';
        while (*cursor == '/' || *cursor == '\\') cursor++;
        if (length == 0 || (length == 1 && segment[0] == '.')) continue;
        vfs_file_t entry;
        if (fat32_find_in_dir(cluster, segment, &entry, 0, 0) != 0 || !(entry.attributes & 0x10)) return -1;
        if (length == 2 && segment[0] == '.' && segment[1] == '.') {
            cluster = entry.first_cluster ? entry.first_cluster : g_fat32.root_cluster;
        } else {
            cluster = entry.first_cluster;
        }
    }
    *out_cluster = cluster;
    return 0;
}

static int fat32_resolve_parent(const char* path, unsigned int* parent_cluster, char* leaf_out) {
    if (!path || !parent_cluster || !leaf_out || !g_fat32.mounted) return -1;
    char parent[FAT32_MAX_PATH];
    unsigned int len = strlen(path);
    if (len >= sizeof(parent)) return -1;
    memcpy(parent, path, len + 1);
    char* leaf = parent + len;
    while (leaf > parent && leaf[-1] != '/' && leaf[-1] != '\\') leaf--;
    unsigned int name_len = strlen(leaf);
    if (name_len == 0 || name_len >= 13) return -1;
    memcpy(leaf_out, leaf, name_len + 1);
    *parent_cluster = g_fat32.cwd_cluster;
    if (leaf > parent) {
        if (leaf - parent == 1) parent[1] = '\0';
        else parent[leaf - parent - 1] = '\0';
        if (fat32_traverse(parent, parent_cluster) != 0) return -1;
    }
    return 0;
}

static int fat32_find_file(const char* path, vfs_file_t* output) {
    unsigned int parent_cluster;
    char leaf[13];
    if (!output || fat32_resolve_parent(path, &parent_cluster, leaf) != 0) return -1;
    return fat32_find_in_dir(parent_cluster, leaf, output, 0, 0);
}

static int fat32_read_at(const vfs_file_t* file, unsigned int offset, void* buffer, unsigned int length) {
    if (!file || (!buffer && length) || !g_fat32.mounted) return -1;
    if (offset >= file->size || length == 0) return 0;
    if (file->first_cluster < 2) return -1;
    unsigned int remaining = file->size - offset;
    if (remaining > length) remaining = length;
    unsigned int cluster_bytes = (unsigned int)g_fat32.sectors_per_cluster * g_fat32.bytes_per_sector;
    unsigned int skip = offset / cluster_bytes;
    unsigned int cluster_offset = offset % cluster_bytes;
    unsigned int cluster = file->first_cluster;
    unsigned int traversed = 0;
    while (skip-- && cluster >= 2 && cluster < FAT32_EOC && traversed++ < g_fat32.total_clusters) {
        cluster = fat32_next_cluster(cluster);
    }
    unsigned int total = 0;
    while (remaining && cluster >= 2 && cluster < FAT32_EOC && traversed++ < g_fat32.total_clusters) {
        unsigned int base_lba = g_fat32.data_lba + (cluster - 2) * g_fat32.sectors_per_cluster;
        unsigned int sector = cluster_offset / g_fat32.bytes_per_sector;
        unsigned int in_sector = cluster_offset % g_fat32.bytes_per_sector;
        while (sector < g_fat32.sectors_per_cluster && remaining) {
            if (ata_select_drive(g_fat32.drive) != 0 || ata_read_sectors(base_lba + sector, 1, g_sector) != 0) return -1;
            unsigned int amount = g_fat32.bytes_per_sector - in_sector;
            if (amount > remaining) amount = remaining;
            memcpy((unsigned char*)buffer + total, (unsigned char*)g_sector + in_sector, amount);
            total += amount;
            remaining -= amount;
            sector++;
            in_sector = 0;
        }
        cluster_offset = 0;
        if (remaining) cluster = fat32_next_cluster(cluster);
    }
    return (int)total;
}

static int fat32_list_cluster(unsigned int start_cluster, vfs_dir_callback_t callback) {
    unsigned int cluster = start_cluster;
    unsigned int traversed = 0;
    while (cluster >= 2 && cluster < FAT32_EOC && traversed++ < g_fat32.total_clusters) {
        unsigned int base_lba = g_fat32.data_lba + (cluster - 2) * g_fat32.sectors_per_cluster;
        for (unsigned int sector = 0; sector < g_fat32.sectors_per_cluster; sector++) {
            if (ata_select_drive(g_fat32.drive) != 0 || ata_read_sectors(base_lba + sector, 1, g_sector) != 0) return -1;
            fat32_raw_entry_t* entries = (fat32_raw_entry_t*)g_sector;
            for (unsigned int i = 0; i < 16; i++) {
                fat32_raw_entry_t* entry = &entries[i];
                if (entry->name[0] == 0) return 0;
                if (entry->name[0] == 0xE5 || entry->attributes == 0x0F || (entry->attributes & 0x08)) continue;
                char name[13];
                fat32_format_name(entry->name, entry->extension, name);
                callback(name, entry->size, (entry->attributes & 0x10) != 0);
            }
        }
        cluster = fat32_next_cluster(cluster);
    }
    return 0;
}

static int fat32_list(const char* path, vfs_dir_callback_t callback) {
    unsigned int cluster = g_fat32.cwd_cluster;
    if (!callback) return -1;
    if (path && path[0] && fat32_traverse(path, &cluster) != 0) return -1;
    return fat32_list_cluster(cluster, callback);
}

static int fat32_chdir(const char* path) {
    unsigned int cluster;
    if (!path || fat32_traverse(path, &cluster) != 0) return -1;
    g_fat32.cwd_cluster = cluster;
    if (path[0] == '/' || path[0] == '\\') {
        unsigned int length = strlen(path);
        if (length >= sizeof(g_fat32.cwd)) return -1;
        memcpy(g_fat32.cwd, path, length + 1);
    } else if (fat32_name_equal(path, "..")) {
        unsigned int length = strlen(g_fat32.cwd);
        while (length > 1 && g_fat32.cwd[length - 1] != '/') length--;
        if (length > 1) length--;
        g_fat32.cwd[length] = '\0';
    } else if (!fat32_name_equal(path, ".")) {
        unsigned int cwd_len = strlen(g_fat32.cwd);
        unsigned int path_len = strlen(path);
        if (cwd_len + path_len + 2 >= sizeof(g_fat32.cwd)) return -1;
        if (cwd_len > 1) g_fat32.cwd[cwd_len++] = '/';
        memcpy(g_fat32.cwd + cwd_len, path, path_len + 1);
    }
    return 0;
}

static int fat32_getcwd(char* buffer, unsigned int capacity) {
    if (!buffer || capacity == 0) return -1;
    unsigned int length = strlen(g_fat32.cwd);
    if (length >= capacity) length = capacity - 1;
    memcpy(buffer, g_fat32.cwd, length);
    buffer[length] = '\0';
    return (int)length;
}

static int fat32_set_fat_entry(unsigned int cluster, unsigned int value) {
    if (cluster < 2 || cluster >= g_fat32.total_clusters + 2) return -1;
    unsigned int byte_offset = cluster * 4;
    unsigned int sector_lba = g_fat32.fat_lba + byte_offset / g_fat32.bytes_per_sector;
    unsigned int index = (byte_offset % g_fat32.bytes_per_sector) / 4;
    for (unsigned int copy = 0; copy < g_fat32.fat_count; copy++) {
        if (ata_select_drive(g_fat32.drive) != 0 ||
            ata_read_sectors(sector_lba + copy * g_fat32.sectors_per_fat, 1, g_sector) != 0) return -1;
        unsigned int* words = (unsigned int*)g_sector;
        words[index] = (words[index] & 0xF0000000) | (value & FAT32_MASK);
        if (ata_write_sectors(sector_lba + copy * g_fat32.sectors_per_fat, 1, g_sector) != 0) return -1;
    }
    g_fat32.cached_fat_lba = 0xFFFFFFFF;
    return 0;
}

static unsigned int fat32_allocate_cluster(void) {
    for (unsigned int cluster = 2; cluster < g_fat32.total_clusters + 2; cluster++) {
        if (fat32_next_cluster(cluster) != 0) continue;
        if (fat32_set_fat_entry(cluster, FAT32_MASK) != 0) return 0;
        unsigned int base_lba = g_fat32.data_lba + (cluster - 2) * g_fat32.sectors_per_cluster;
        for (unsigned int sector = 0; sector < g_fat32.sectors_per_cluster; sector++) {
            memset(g_sector, 0, sizeof(g_sector));
            if (ata_write_sectors(base_lba + sector, 1, g_sector) != 0) return 0;
        }
        return cluster;
    }
    return 0;
}

static void fat32_free_chain(unsigned int cluster) {
    unsigned int traversed = 0;
    while (cluster >= 2 && cluster < FAT32_EOC && traversed++ < g_fat32.total_clusters) {
        unsigned int next = fat32_next_cluster(cluster);
        if (fat32_set_fat_entry(cluster, 0) != 0) return;
        cluster = next;
    }
}

static int fat32_find_free_slot(unsigned int directory_cluster, unsigned int* out_lba, unsigned int* out_slot) {
    unsigned int cluster = directory_cluster;
    unsigned int last_cluster = cluster;
    unsigned int traversed = 0;
    while (cluster >= 2 && cluster < FAT32_EOC && traversed++ < g_fat32.total_clusters) {
        unsigned int base_lba = g_fat32.data_lba + (cluster - 2) * g_fat32.sectors_per_cluster;
        for (unsigned int sector = 0; sector < g_fat32.sectors_per_cluster; sector++) {
            if (ata_read_sectors(base_lba + sector, 1, g_sector) != 0) return -1;
            fat32_raw_entry_t* entries = (fat32_raw_entry_t*)g_sector;
            for (unsigned int i = 0; i < 16; i++) {
                if (entries[i].name[0] == 0 || entries[i].name[0] == 0xE5) {
                    *out_lba = base_lba + sector;
                    *out_slot = i;
                    return 0;
                }
            }
        }
        last_cluster = cluster;
        cluster = fat32_next_cluster(cluster);
    }
    if (traversed >= g_fat32.total_clusters) return -1;
    unsigned int appended = fat32_allocate_cluster();
    if (!appended || fat32_set_fat_entry(last_cluster, appended) != 0) return -1;
    *out_lba = g_fat32.data_lba + (appended - 2) * g_fat32.sectors_per_cluster;
    *out_slot = 0;
    return 0;
}

static void fat32_encode_name(const char* name, unsigned char base[8], unsigned char ext[3]) {
    fat32_format_83(name, base, ext);
}

static int fat32_write_file(const char* path, const void* data, unsigned int size) {
    if (!g_fat32.mounted || (!data && size)) return -1;
    unsigned int parent_cluster, dir_lba, slot;
    char leaf[13];
    if (fat32_resolve_parent(path, &parent_cluster, leaf) != 0) return -1;

    vfs_file_t existing;
    unsigned int first_cluster = 0;
    if (fat32_find_in_dir(parent_cluster, leaf, &existing, &dir_lba, &slot) == 0) {
        if (existing.attributes & 0x10) return -1;
        first_cluster = existing.first_cluster;
        if (first_cluster >= 2) fat32_free_chain(first_cluster);
        first_cluster = 0;
    } else if (fat32_find_free_slot(parent_cluster, &dir_lba, &slot) != 0) {
        return -1;
    }

    unsigned int bytes_written = 0;
    unsigned int previous = 0;
    while (bytes_written < size) {
        unsigned int cluster = fat32_allocate_cluster();
        if (!cluster) {
            if (first_cluster >= 2) fat32_free_chain(first_cluster);
            return -1;
        }
        if (!first_cluster) first_cluster = cluster;
        if (previous && fat32_set_fat_entry(previous, cluster) != 0) return -1;
        previous = cluster;
        unsigned int base_lba = g_fat32.data_lba + (cluster - 2) * g_fat32.sectors_per_cluster;
        for (unsigned int sector = 0; sector < g_fat32.sectors_per_cluster && bytes_written < size; sector++) {
            unsigned int amount = size - bytes_written;
            if (amount > g_fat32.bytes_per_sector) amount = g_fat32.bytes_per_sector;
            memset(g_sector, 0, sizeof(g_sector));
            memcpy(g_sector, (const unsigned char*)data + bytes_written, amount);
            if (ata_write_sectors(base_lba + sector, 1, g_sector) != 0) return -1;
            bytes_written += amount;
        }
    }

    if (ata_read_sectors(dir_lba, 1, g_sector) != 0) return -1;
    fat32_raw_entry_t* entry = &((fat32_raw_entry_t*)g_sector)[slot];
    fat32_encode_name(leaf, entry->name, entry->extension);
    entry->attributes = 0x20;
    entry->first_cluster_high = (unsigned short)(first_cluster >> 16);
    entry->first_cluster_low = (unsigned short)first_cluster;
    entry->size = size;
    return ata_write_sectors(dir_lba, 1, g_sector);
}

static int fat32_delete_file(const char* path) {
    unsigned int parent_cluster, dir_lba, slot;
    char leaf[13];
    vfs_file_t entry;
    if (fat32_resolve_parent(path, &parent_cluster, leaf) != 0 ||
        fat32_find_in_dir(parent_cluster, leaf, &entry, &dir_lba, &slot) != 0 ||
        (entry.attributes & 0x10) || ata_read_sectors(dir_lba, 1, g_sector) != 0) return -1;
    if (entry.first_cluster >= 2) fat32_free_chain(entry.first_cluster);
    ((fat32_raw_entry_t*)g_sector)[slot].name[0] = 0xE5;
    return ata_write_sectors(dir_lba, 1, g_sector);
}

static int fat32_vfs_mkdir(const char* path) {
    unsigned int parent_cluster, dir_lba, slot;
    char leaf[13];
    vfs_file_t existing;
    if (fat32_resolve_parent(path, &parent_cluster, leaf) != 0 ||
        fat32_find_in_dir(parent_cluster, leaf, &existing, 0, 0) == 0) return -1;
    if (fat32_find_free_slot(parent_cluster, &dir_lba, &slot) != 0) return -1;

    unsigned int new_cluster = fat32_allocate_cluster();
    if (!new_cluster) return -1;
    unsigned int data_lba = g_fat32.data_lba + (new_cluster - 2) * g_fat32.sectors_per_cluster;
    if (ata_read_sectors(data_lba, 1, g_sector) != 0) return -1;
    fat32_raw_entry_t* entries = (fat32_raw_entry_t*)g_sector;
    memset(&entries[0], 0, sizeof(entries[0]));
    memset(entries[0].name, ' ', sizeof(entries[0].name));
    memset(entries[0].extension, ' ', sizeof(entries[0].extension));
    entries[0].name[0] = '.';
    entries[0].attributes = 0x10;
    entries[0].first_cluster_high = (unsigned short)(new_cluster >> 16);
    entries[0].first_cluster_low = (unsigned short)new_cluster;
    memset(&entries[1], 0, sizeof(entries[1]));
    memset(entries[1].name, ' ', sizeof(entries[1].name));
    memset(entries[1].extension, ' ', sizeof(entries[1].extension));
    entries[1].name[0] = '.';
    entries[1].name[1] = '.';
    entries[1].attributes = 0x10;
    entries[1].first_cluster_high = (unsigned short)(parent_cluster >> 16);
    entries[1].first_cluster_low = (unsigned short)parent_cluster;
    if (ata_write_sectors(data_lba, 1, g_sector) != 0 || ata_read_sectors(dir_lba, 1, g_sector) != 0) return -1;

    fat32_raw_entry_t* entry = &((fat32_raw_entry_t*)g_sector)[slot];
    memset(entry, 0, sizeof(*entry));
    fat32_encode_name(leaf, entry->name, entry->extension);
    entry->attributes = 0x10;
    entry->first_cluster_high = (unsigned short)(new_cluster >> 16);
    entry->first_cluster_low = (unsigned short)new_cluster;
    return ata_write_sectors(dir_lba, 1, g_sector);
}

static int fat32_vfs_rmdir(const char* path) {
    unsigned int parent_cluster, dir_lba, slot;
    char leaf[13];
    vfs_file_t directory;
    if (fat32_resolve_parent(path, &parent_cluster, leaf) != 0 ||
        fat32_find_in_dir(parent_cluster, leaf, &directory, &dir_lba, &slot) != 0 ||
        !(directory.attributes & 0x10) || directory.first_cluster == g_fat32.cwd_cluster) return -1;

    unsigned int cluster = directory.first_cluster;
    unsigned int traversed = 0;
    while (cluster >= 2 && cluster < FAT32_EOC && traversed++ < g_fat32.total_clusters) {
        unsigned int base_lba = g_fat32.data_lba + (cluster - 2) * g_fat32.sectors_per_cluster;
        for (unsigned int sector = 0; sector < g_fat32.sectors_per_cluster; sector++) {
            if (ata_read_sectors(base_lba + sector, 1, g_sector) != 0) return -1;
            fat32_raw_entry_t* entries = (fat32_raw_entry_t*)g_sector;
            for (unsigned int i = 0; i < 16; i++) {
                fat32_raw_entry_t* entry = &entries[i];
                if (entry->name[0] == 0) break;
                if (entry->name[0] == 0xE5 || entry->attributes == 0x0F) continue;
                if ((entry->name[0] == '.' && entry->name[1] == ' ') ||
                    (entry->name[0] == '.' && entry->name[1] == '.' && entry->name[2] == ' ')) continue;
                return -1;
            }
        }
        cluster = fat32_next_cluster(cluster);
    }
    fat32_free_chain(directory.first_cluster);
    if (ata_read_sectors(dir_lba, 1, g_sector) != 0) return -1;
    ((fat32_raw_entry_t*)g_sector)[slot].name[0] = 0xE5;
    return ata_write_sectors(dir_lba, 1, g_sector);
}

static int fat32_mount_root(void) {
    if (!ata_probe_drive(0) || ata_select_drive(0) != 0 || ata_read_sectors(0, 1, g_sector) != 0) return -1;
    fat32_partition_t* partitions = (fat32_partition_t*)((unsigned char*)g_sector + 446);
    for (unsigned int i = 0; i < 4; i++) {
        if (partitions[i].lba_start && partitions[i].sectors &&
            fat32_mount_at(0, partitions[i].lba_start, partitions[i].sectors) == 0) return 0;
    }
    if (fat32_mount_at(0, 0, 0) == 0) return 0;
    static const unsigned int offsets[] = {2048, 64, 128, 32};
    for (unsigned int i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++) {
        if (fat32_mount_at(0, offsets[i], 0) == 0) return 0;
    }
    return -1;
}

int fat32_init(void) { return fat32_mount_root(); }

static int fat32_vfs_mount(void) { return fat32_mount_root(); }
static int fat32_vfs_find(const char* path, vfs_file_t* file) { return fat32_find_file(path, file); }
static int fat32_vfs_read(const vfs_file_t* file, unsigned int offset, void* buffer, unsigned int length) {
    return fat32_read_at(file, offset, buffer, length);
}
const vfs_driver_t fat32_vfs_driver = {
    .name = "fat32",
    .mount_root = fat32_vfs_mount,
    .find_file = fat32_vfs_find,
    .read_at = fat32_vfs_read,
    .write_file = fat32_write_file,
    .delete_file = fat32_delete_file,
    .list_directory = fat32_list,
    .mkdir = fat32_vfs_mkdir,
    .rmdir = fat32_vfs_rmdir,
    .chdir = fat32_chdir,
    .getcwd = fat32_getcwd,
};