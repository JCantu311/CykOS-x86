#include "fat16.h"
#include "ata.h"
#include "cykastd.h"

// Raw on-disk FAT16 BIOS Parameter Block (512 bytes total)
typedef struct __attribute__((packed)) {
    unsigned char jmp[3];
    char oem_name[8];
    unsigned short bytes_per_sector;
    unsigned char sectors_per_cluster;
    unsigned short reserved_sectors;
    unsigned char fat_count;
    unsigned short root_dir_entries;
    unsigned short total_sectors_16;
    unsigned char media_descriptor;
    unsigned short sectors_per_fat;
    unsigned short sectors_per_track;
    unsigned short head_count;
    unsigned int hidden_sectors;
    unsigned int total_sectors_32;
    unsigned char drive_number;
    unsigned char reserved1;
    unsigned char boot_signature;
    unsigned int volume_id;
    char volume_label[11];
    char fs_type[8];
} fat16_bpb_t;

// Raw 32-byte directory entry on disk
typedef struct __attribute__((packed)) {
    char name[8];
    char ext[3];
    unsigned char attributes;
    unsigned char reserved;
    unsigned char create_time_tenth;
    unsigned short create_time;
    unsigned short create_date;
    unsigned short last_access_date;
    unsigned short first_cluster_high;
    unsigned short write_time;
    unsigned short write_date;
    unsigned short first_cluster_low;
    unsigned int file_size;
} fat16_raw_entry_t;

// Raw 16-byte MBR partition entry
typedef struct __attribute__((packed)) {
    unsigned char status;
    unsigned char chs_start[3];
    unsigned char partition_type;
    unsigned char chs_end[3];
    unsigned int lba_start;
    unsigned int sector_count;
} mbr_partition_t;

// Runtime state for the mounted FAT16 volume
typedef struct {
    unsigned int partition_lba;
    unsigned short bytes_per_sector;
    unsigned char sectors_per_cluster;
    unsigned short reserved_sectors;
    unsigned char fat_count;
    unsigned short root_dir_entries;
    unsigned short sectors_per_fat;
    unsigned int fat_lba;
    unsigned int root_dir_lba;
    unsigned int root_dir_sectors;
    unsigned int data_lba;
    char volume_label[12];
    int is_mounted;
} fat16_state_t;

static fat16_state_t g_fat16;
static unsigned short g_cwd_cluster = 0; // 0 = Root directory
static char g_cwd_path[128] = "\\";      // Current working directory path
static unsigned short g_sector_buf[256];      // 512-byte scratch buffer
static unsigned short g_fat_cache_buf[256];  // 512-byte FAT sector cache
static unsigned int g_cached_fat_lba = 0xFFFFFFFF;

// Convert a character to uppercase
static char to_upper(char c) {
    if (c >= 'a' && c <= 'z') {
        return c - ('a' - 'A');
    }
    return c;
}

// Convert user-supplied filename (e.g. "keyboard.bin") into raw 8.3 padded format (e.g. "KEYBOARD   BIN")
static void format_to_8_3(const char* input, char* out_name, char* out_ext) {
    memset(out_name, ' ', 8);
    memset(out_ext, ' ', 3);

    int i = 0;
    while (input[i] != '\0' && input[i] != '.' && i < 8) {
        out_name[i] = to_upper(input[i]);
        i++;
    }

    // Skip to extension if any
    const char* dot = input;
    while (*dot != '\0' && *dot != '.') {
        dot++;
    }

    if (*dot == '.') {
        dot++;
        for (int j = 0; j < 3 && dot[j] != '\0'; j++) {
            out_ext[j] = to_upper(dot[j]);
        }
    }
}

// Format raw 8.3 entry into a standard null-terminated string (e.g. "KEYBOARD.BIN")
static void format_raw_to_string(const char* raw_name, const char* raw_ext, char* out_str) {
    int pos = 0;
    for (int i = 0; i < 8 && raw_name[i] != ' '; i++) {
        out_str[pos++] = raw_name[i];
    }
    if (raw_ext[0] != ' ') {
        out_str[pos++] = '.';
        for (int i = 0; i < 3 && raw_ext[i] != ' '; i++) {
            out_str[pos++] = raw_ext[i];
        }
    }
    out_str[pos] = '\0';
}

// Read next cluster in the FAT chain
static unsigned short fat16_get_next_cluster(unsigned short current_cluster) {
    unsigned int fat_offset_bytes = current_cluster * 2;
    unsigned int target_sector_lba = g_fat16.fat_lba + (fat_offset_bytes / g_fat16.bytes_per_sector);
    unsigned int offset_in_sector = (fat_offset_bytes % g_fat16.bytes_per_sector) / 2;

    if (g_cached_fat_lba != target_sector_lba) {
        if (ata_read_sectors(target_sector_lba, 1, g_fat_cache_buf) != 0) {
            return 0xFFFF; // Error reading FAT
        }
        g_cached_fat_lba = target_sector_lba;
    }

    return g_fat_cache_buf[offset_in_sector];
}

// Verify whether a given sector looks like a valid FAT16 BPB
static int check_bpb_at_lba(unsigned int lba) {
    if (ata_read_sectors(lba, 1, g_sector_buf) != 0) {
        return 0;
    }

    fat16_bpb_t* bpb = (fat16_bpb_t*)g_sector_buf;

    // Check fundamental FAT sanity checks
    if (bpb->bytes_per_sector != 512) {
        return 0;
    }
    if (bpb->sectors_per_cluster == 0 || bpb->reserved_sectors == 0) {
        return 0;
    }
    if (bpb->fat_count == 0 || bpb->sectors_per_fat == 0) {
        return 0;
    }
    if (bpb->root_dir_entries == 0) {
        return 0;
    }

    // Check FS type string or signature
    if (strncmp(bpb->fs_type, "FAT16", 5) == 0 || bpb->boot_signature == 0x29 || bpb->boot_signature == 0x28) {
        g_fat16.partition_lba = lba;
        g_fat16.bytes_per_sector = bpb->bytes_per_sector;
        g_fat16.sectors_per_cluster = bpb->sectors_per_cluster;
        g_fat16.reserved_sectors = bpb->reserved_sectors;
        g_fat16.fat_count = bpb->fat_count;
        g_fat16.root_dir_entries = bpb->root_dir_entries;
        g_fat16.sectors_per_fat = bpb->sectors_per_fat;

        g_fat16.fat_lba = lba + bpb->reserved_sectors;
        g_fat16.root_dir_lba = g_fat16.fat_lba + (bpb->fat_count * bpb->sectors_per_fat);
        g_fat16.root_dir_sectors = ((bpb->root_dir_entries * 32) + (bpb->bytes_per_sector - 1)) / bpb->bytes_per_sector;
        g_fat16.data_lba = g_fat16.root_dir_lba + g_fat16.root_dir_sectors;

        memcpy(g_fat16.volume_label, bpb->volume_label, 11);
        g_fat16.volume_label[11] = '\0';
        g_fat16.is_mounted = 1;
        g_cached_fat_lba = 0xFFFFFFFF;
        return 1;
    }

    return 0;
}

/**
 * Automatically locates and mounts a FAT16 filesystem on the primary drive.
 * Supports MBR partition tables, raw superfloppy volumes, and common partition offsets.
 * @return 0 on success, -1 on failure
 */
int fat16_init(void) {
    g_fat16.is_mounted = 0;

    // 1. Read LBA 0 to check if it's an MBR with a partition table
    if (ata_read_sectors(0, 1, g_sector_buf) == 0) {
        unsigned char* raw = (unsigned char*)g_sector_buf;

        // Check partition 1 at offset 446 (0x1BE)
        mbr_partition_t* part1 = (mbr_partition_t*)(raw + 446);
        if (part1->lba_start > 0 && part1->sector_count > 0) {
            if (check_bpb_at_lba(part1->lba_start)) {
                g_cwd_cluster = 0;
                g_cwd_path[0] = '\\';
                g_cwd_path[1] = '\0';
                return 0; // Successfully mounted from MBR partition 1
            }
        }

        // 2. Check if LBA 0 itself is a raw FAT16 volume (superfloppy mode)
        if (check_bpb_at_lba(0)) {
            g_cwd_cluster = 0;
            g_cwd_path[0] = '\\';
            g_cwd_path[1] = '\0';
            return 0;
        }
    }

    // 3. Fallback scan for common partition offsets (e.g., LBA 64, 128, 2048)
    unsigned int scan_offsets[] = {64, 128, 2048, 32};
    for (unsigned int i = 0; i < sizeof(scan_offsets) / sizeof(scan_offsets[0]); i++) {
        if (check_bpb_at_lba(scan_offsets[i])) {
            g_cwd_cluster = 0;
            g_cwd_path[0] = '\\';
            g_cwd_path[1] = '\0';
            return 0;
        }
    }

    return -1; // No FAT16 filesystem detected
}

// Context for scanning a directory's sectors across Root or Subdirectory cluster chains
typedef struct {
    unsigned short dir_cluster;
    unsigned short curr_cluster;
    unsigned int sector_idx;
    int is_root;
    int is_end;
} fat16_dir_iter_t;

static void fat16_dir_iter_init(fat16_dir_iter_t* iter, unsigned short dir_cluster) {
    iter->dir_cluster = dir_cluster;
    iter->is_root = (dir_cluster == 0);
    iter->curr_cluster = dir_cluster;
    iter->sector_idx = 0;
    iter->is_end = 0;
}

static unsigned int fat16_dir_iter_next_sector(fat16_dir_iter_t* iter) {
    if (iter->is_end) {
        return 0;
    }

    if (iter->is_root) {
        if (iter->sector_idx >= g_fat16.root_dir_sectors) {
            iter->is_end = 1;
            return 0;
        }
        unsigned int lba = g_fat16.root_dir_lba + iter->sector_idx;
        iter->sector_idx++;
        return lba;
    } else {
        while (iter->curr_cluster >= 2 && iter->curr_cluster < 0xFFF8) {
            if (iter->sector_idx < g_fat16.sectors_per_cluster) {
                unsigned int cluster_lba = g_fat16.data_lba + (iter->curr_cluster - 2) * g_fat16.sectors_per_cluster;
                unsigned int lba = cluster_lba + iter->sector_idx;
                iter->sector_idx++;
                return lba;
            }
            // Move to next cluster in subdirectory chain
            iter->curr_cluster = fat16_get_next_cluster(iter->curr_cluster);
            iter->sector_idx = 0;
        }
        iter->is_end = 1;
        return 0;
    }
}

// Low-level helper: search a specific directory cluster for a target 8.3 entry
static int fat16_find_in_dir(unsigned short dir_cluster, const char* target_name, const char* target_ext, fat16_entry_t* out_entry, unsigned int* out_sector_lba, int* out_slot_index) {
    fat16_dir_iter_t iter;
    fat16_dir_iter_init(&iter, dir_cluster);

    unsigned int lba;
    while ((lba = fat16_dir_iter_next_sector(&iter)) != 0) {
        if (ata_read_sectors(lba, 1, g_sector_buf) != 0) {
            return -1;
        }

        fat16_raw_entry_t* entries = (fat16_raw_entry_t*)g_sector_buf;
        for (int i = 0; i < 16; i++) {
            unsigned char first_byte = (unsigned char)entries[i].name[0];
            if (first_byte == 0x00) {
                return -1; // End of directory
            }
            if (first_byte == 0xE5) {
                continue; // Deleted
            }
            if (entries[i].attributes == FAT_ATTR_LFN || (entries[i].attributes & FAT_ATTR_VOLUME_ID)) {
                continue;
            }

            if (strncmp(entries[i].name, target_name, 8) == 0 &&
                strncmp(entries[i].ext, target_ext, 3) == 0) {
                if (out_entry) {
                    format_raw_to_string(entries[i].name, entries[i].ext, out_entry->filename);
                    out_entry->file_size = entries[i].file_size;
                    out_entry->first_cluster = entries[i].first_cluster_low;
                    out_entry->attributes = entries[i].attributes;
                }
                if (out_sector_lba) *out_sector_lba = lba;
                if (out_slot_index) *out_slot_index = i;
                return 0;
            }
        }
    }

    return -1;
}

// Resolves a directory path to its starting cluster number.
// Supports absolute paths ("/DIR", "\DIR", "C:\DIR") and relative paths ("DIR", "..", "DIR/SUBDIR").
static int fat16_traverse_path(const char* path, unsigned short* out_cluster) {
    if (!g_fat16.is_mounted) return -1;
    if (!path || path[0] == '\0') {
        if (out_cluster) *out_cluster = g_cwd_cluster;
        return 0;
    }

    const char* p = path;
    // Skip drive letter (e.g. "C:" or "c:")
    if ((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) {
        if (p[1] == ':') {
            p += 2;
        }
    }

    unsigned short curr_cluster = g_cwd_cluster;
    if (*p == '/' || *p == '\\') {
        curr_cluster = 0; // Absolute path starts at Root
        while (*p == '/' || *p == '\\') p++;
    }

    if (*p == '\0') {
        if (out_cluster) *out_cluster = curr_cluster;
        return 0;
    }

    while (*p != '\0') {
        char seg[16];
        int seg_len = 0;
        while (*p != '\0' && *p != '/' && *p != '\\' && seg_len < 15) {
            seg[seg_len++] = *p++;
        }
        seg[seg_len] = '\0';
        while (*p == '/' || *p == '\\') p++;

        if (seg_len == 0 || (seg_len == 1 && seg[0] == '.')) {
            continue; // Current directory '.'
        }

        if (seg_len == 2 && seg[0] == '.' && seg[1] == '.') {
            // Move up to parent '..'
            if (curr_cluster == 0) {
                curr_cluster = 0;
            } else {
                fat16_entry_t parent_entry;
                if (fat16_find_in_dir(curr_cluster, "..      ", "   ", &parent_entry, (void*)0, (void*)0) != 0) {
                    return -1;
                }
                curr_cluster = parent_entry.first_cluster;
            }
            continue;
        }

        char target_name[8];
        char target_ext[3];
        format_to_8_3(seg, target_name, target_ext);

        fat16_entry_t entry;
        if (fat16_find_in_dir(curr_cluster, target_name, target_ext, &entry, (void*)0, (void*)0) != 0) {
            return -1;
        }

        if (!(entry.attributes & FAT_ATTR_DIRECTORY)) {
            return -2; // Not a directory
        }

        curr_cluster = entry.first_cluster;
    }

    if (out_cluster) *out_cluster = curr_cluster;
    return 0;
}

// Split a path into parent directory cluster and leaf filename.
static int fat16_resolve_parent_and_leaf(const char* path, unsigned short* out_dir_cluster, char* out_leaf, int max_leaf) {
    if (!path) return -1;

    const char* last_slash = (void*)0;
    const char* p = path;
    while (*p != '\0') {
        if (*p == '/' || *p == '\\') {
            last_slash = p;
        }
        p++;
    }

    if (!last_slash) {
        if (out_dir_cluster) *out_dir_cluster = g_cwd_cluster;
        int i = 0;
        while (path[i] != '\0' && i < max_leaf - 1) {
            out_leaf[i] = path[i];
            i++;
        }
        out_leaf[i] = '\0';
        return 0;
    }

    const char* leaf_start = last_slash + 1;
    int i = 0;
    while (leaf_start[i] != '\0' && i < max_leaf - 1) {
        out_leaf[i] = leaf_start[i];
        i++;
    }
    out_leaf[i] = '\0';

    char dir_path[128];
    int dir_len = (int)(last_slash - path);
    if (dir_len == 0) {
        dir_path[0] = '/';
        dir_path[1] = '\0';
    } else {
        if (dir_len > 127) dir_len = 127;
        for (int j = 0; j < dir_len; j++) {
            dir_path[j] = path[j];
        }
        dir_path[dir_len] = '\0';
    }

    return fat16_traverse_path(dir_path, out_dir_cluster);
}

/**
 * Searches for a specific file by path (supports both relative and absolute paths).
 */
int fat16_find_file(const char* filename, fat16_entry_t* out_entry) {
    if (!g_fat16.is_mounted || !filename || !out_entry) {
        return -1;
    }

    unsigned short parent_cluster = 0;
    char leaf[32];
    if (fat16_resolve_parent_and_leaf(filename, &parent_cluster, leaf, sizeof(leaf)) != 0) {
        return -1;
    }

    char target_name[8];
    char target_ext[3];
    format_to_8_3(leaf, target_name, target_ext);

    return fat16_find_in_dir(parent_cluster, target_name, target_ext, out_entry, (void*)0, (void*)0);
}

/**
 * Reads the full content of a file into a destination buffer.
 * @param entry      File entry obtained from fat16_find_file
 * @param buffer     Target memory address to copy file data into
 * @param max_bytes  Maximum bytes to copy (buffer limit)
 * @return           Total bytes read, or -1 on error
 */
int fat16_read_file(const fat16_entry_t* entry, void* buffer, unsigned int max_bytes) {
    if (!g_fat16.is_mounted || !entry || !buffer || entry->first_cluster < 2) {
        return -1;
    }

    unsigned char* dest = (unsigned char*)buffer;
    unsigned short cluster = entry->first_cluster;
    unsigned int bytes_remaining = entry->file_size;
    if (bytes_remaining > max_bytes) {
        bytes_remaining = max_bytes;
    }

    unsigned int total_read = 0;

    while (cluster >= 2 && cluster < 0xFFF8 && bytes_remaining > 0) {
        unsigned int cluster_lba = g_fat16.data_lba + (cluster - 2) * g_fat16.sectors_per_cluster;

        // Read all sectors of this cluster
        for (unsigned char s = 0; s < g_fat16.sectors_per_cluster && bytes_remaining > 0; s++) {
            if (ata_read_sectors(cluster_lba + s, 1, g_sector_buf) != 0) {
                return -1;
            }

            unsigned int chunk = (bytes_remaining < g_fat16.bytes_per_sector) ? bytes_remaining : g_fat16.bytes_per_sector;
            memcpy(dest + total_read, g_sector_buf, chunk);
            total_read += chunk;
            bytes_remaining -= chunk;
        }

        cluster = fat16_get_next_cluster(cluster);
    }

    return (int)total_read;
}

/**
 * Iterates through directory entries in any directory cluster (0 for Root, >= 2 for subdirectories)
 * and triggers the callback for each.
 */
int fat16_list_directory_cluster(unsigned short dir_cluster, fat16_dir_callback_t callback) {
    if (!g_fat16.is_mounted || !callback) {
        return -1;
    }

    char display_name[13];
    fat16_dir_iter_t iter;
    fat16_dir_iter_init(&iter, dir_cluster);

    unsigned int lba;
    while ((lba = fat16_dir_iter_next_sector(&iter)) != 0) {
        if (ata_read_sectors(lba, 1, g_sector_buf) != 0) {
            return -1;
        }

        fat16_raw_entry_t* entries = (fat16_raw_entry_t*)g_sector_buf;
        for (int i = 0; i < 16; i++) {
            if ((unsigned char)entries[i].name[0] == 0x00) {
                return 0; // End of directory
            }
            if ((unsigned char)entries[i].name[0] == 0xE5) {
                continue; // Deleted entry
            }
            if (entries[i].attributes == FAT_ATTR_LFN || (entries[i].attributes & FAT_ATTR_VOLUME_ID)) {
                continue;
            }

            format_raw_to_string(entries[i].name, entries[i].ext, display_name);
            unsigned char is_dir = (entries[i].attributes & FAT_ATTR_DIRECTORY) ? 1 : 0;
            callback(display_name, entries[i].file_size, is_dir);
        }
    }

    return 0;
}

/**
 * Lists the current working directory.
 */
int fat16_list_directory(fat16_dir_callback_t callback) {
    return fat16_list_directory_cluster(g_cwd_cluster, callback);
}

/**
 * Prints volume information to console.
 */
void fat16_print_volume_info(void) {
    if (!g_fat16.is_mounted) {
        print_color_string("FAT16: Not mounted.\n", VGA_RED);
        return;
    }

    print_color_string("FAT16 Volume Mounted: ", VGA_GREEN);
    if (g_fat16.volume_label[0] != '\0') {
        print_string(g_fat16.volume_label);
    } else {
        print_string("NO_NAME");
    }
    print_string("\n");
}

static int fat16_set_cluster_entry(unsigned short cluster, unsigned short value) {
    if (cluster < 2) return -1;
    unsigned int fat_offset_bytes = cluster * 2;
    unsigned int target_sector_lba = g_fat16.fat_lba + (fat_offset_bytes / g_fat16.bytes_per_sector);
    unsigned int offset_in_sector = (fat_offset_bytes % g_fat16.bytes_per_sector) / 2;

    if (ata_read_sectors(target_sector_lba, 1, g_sector_buf) != 0) {
        return -1;
    }
    g_sector_buf[offset_in_sector] = value;

    if (ata_write_sectors(target_sector_lba, 1, g_sector_buf) != 0) {
        return -1;
    }

    if (g_fat16.fat_count > 1) {
        unsigned int fat2_sector_lba = target_sector_lba + g_fat16.sectors_per_fat;
        if (ata_write_sectors(fat2_sector_lba, 1, g_sector_buf) != 0) {
            return -1;
        }
    }

    g_cached_fat_lba = 0xFFFFFFFF;
    return 0;
}

static unsigned short fat16_find_free_cluster(void) {
    unsigned int total_entries = (unsigned int)g_fat16.sectors_per_fat * (g_fat16.bytes_per_sector / 2);
    for (unsigned short c = 2; c < total_entries && c < 0xFFF0; c++) {
        if (fat16_get_next_cluster(c) == 0x0000) {
            return c;
        }
    }
    return 0xFFFF;
}

static void fat16_free_cluster_chain(unsigned short start_cluster) {
    unsigned short curr = start_cluster;
    while (curr >= 2 && curr < 0xFFF8) {
        unsigned short next = fat16_get_next_cluster(curr);
        fat16_set_cluster_entry(curr, 0x0000);
        if (next == 0x0000 || next >= 0xFFF8) {
            break;
        }
        curr = next;
    }
}

/**
 * Writes or creates a file on the FAT16 volume.
 * Supports subdirectories, relative and absolute paths, creating new files and overwriting.
 */
int fat16_write_file(const char* filename, const void* data, unsigned int size) {
    if (!g_fat16.is_mounted || !filename) {
        return -1;
    }

    unsigned short parent_cluster = 0;
    char leaf[32];
    if (fat16_resolve_parent_and_leaf(filename, &parent_cluster, leaf, sizeof(leaf)) != 0) {
        return -1;
    }

    char target_name[8];
    char target_ext[3];
    format_to_8_3(leaf, target_name, target_ext);

    int entry_found = 0;
    unsigned int found_dir_sector = 0;
    int found_dir_index = 0;
    unsigned short old_first_cluster = 0;

    int free_slot_found = 0;
    unsigned int free_dir_sector = 0;
    int free_dir_index = 0;

    fat16_dir_iter_t iter;
    fat16_dir_iter_init(&iter, parent_cluster);
    unsigned int dir_lba;

    while ((dir_lba = fat16_dir_iter_next_sector(&iter)) != 0) {
        if (ata_read_sectors(dir_lba, 1, g_sector_buf) != 0) {
            return -2;
        }

        fat16_raw_entry_t* entries = (fat16_raw_entry_t*)g_sector_buf;
        for (int i = 0; i < 16; i++) {
            unsigned char first_byte = (unsigned char)entries[i].name[0];

            if (first_byte == 0x00 || first_byte == 0xE5) {
                if (!free_slot_found) {
                    free_slot_found = 1;
                    free_dir_sector = dir_lba;
                    free_dir_index = i;
                }
                if (first_byte == 0x00) {
                    break;
                }
                continue;
            }

            if (entries[i].attributes == FAT_ATTR_LFN || (entries[i].attributes & FAT_ATTR_VOLUME_ID)) {
                continue;
            }

            if (strncmp(entries[i].name, target_name, 8) == 0 &&
                strncmp(entries[i].ext, target_ext, 3) == 0) {
                entry_found = 1;
                found_dir_sector = dir_lba;
                found_dir_index = i;
                old_first_cluster = entries[i].first_cluster_low;
                break;
            }
        }

        if (entry_found) break;
    }

    if (!entry_found && !free_slot_found) {
        return -3; // Directory full
    }

    // If overwriting, free old cluster chain
    if (entry_found && old_first_cluster >= 2) {
        fat16_free_cluster_chain(old_first_cluster);
    }

    // Allocate new clusters and write data
    unsigned short first_cluster = 0;
    unsigned short prev_cluster = 0;
    unsigned int bytes_written = 0;
    const unsigned char* src = (const unsigned char*)data;

    if (size > 0 && src != (void*)0) {
        while (bytes_written < size) {
            unsigned short new_cluster = fat16_find_free_cluster();
            if (new_cluster == 0xFFFF) {
                if (first_cluster >= 2) {
                    fat16_free_cluster_chain(first_cluster);
                }
                return -4; // Disk full
            }

            if (fat16_set_cluster_entry(new_cluster, 0xFFFF) != 0) {
                return -5;
            }

            if (first_cluster == 0) {
                first_cluster = new_cluster;
            } else {
                if (fat16_set_cluster_entry(prev_cluster, new_cluster) != 0) {
                    return -5;
                }
            }
            prev_cluster = new_cluster;

            unsigned int cluster_lba = g_fat16.data_lba + (new_cluster - 2) * g_fat16.sectors_per_cluster;
            for (unsigned char s = 0; s < g_fat16.sectors_per_cluster; s++) {
                memset(g_sector_buf, 0, sizeof(g_sector_buf));
                if (bytes_written < size) {
                    unsigned int chunk = size - bytes_written;
                    if (chunk > g_fat16.bytes_per_sector) {
                        chunk = g_fat16.bytes_per_sector;
                    }
                    memcpy(g_sector_buf, src + bytes_written, chunk);
                    bytes_written += chunk;
                }
                if (ata_write_sectors(cluster_lba + s, 1, g_sector_buf) != 0) {
                    return -6;
                }
            }
        }
    }

    // Update directory entry
    unsigned int target_dir_sector = entry_found ? found_dir_sector : free_dir_sector;
    int target_dir_index = entry_found ? found_dir_index : free_dir_index;

    if (ata_read_sectors(target_dir_sector, 1, g_sector_buf) != 0) {
        return -7;
    }

    fat16_raw_entry_t* dir_entries = (fat16_raw_entry_t*)g_sector_buf;
    fat16_raw_entry_t* target_entry = &dir_entries[target_dir_index];

    memcpy(target_entry->name, target_name, 8);
    memcpy(target_entry->ext, target_ext, 3);
    target_entry->attributes = FAT_ATTR_ARCHIVE;
    target_entry->reserved = 0;
    target_entry->first_cluster_high = 0;
    target_entry->first_cluster_low = first_cluster;
    target_entry->file_size = size;

    if (ata_write_sectors(target_dir_sector, 1, g_sector_buf) != 0) {
        return -8;
    }

    g_cached_fat_lba = 0xFFFFFFFF;
    return 0;
}

/**
 * Convenience helper to read a file by its filename or path directly.
 */
int fat16_read_file_by_name(const char* filename, void* buffer, unsigned int max_bytes) {
    if (!filename || !buffer) return -1;
    fat16_entry_t entry;
    if (fat16_find_file(filename, &entry) != 0) {
        return -1;
    }
    return fat16_read_file(&entry, buffer, max_bytes);
}

/**
 * Deletes a file from the FAT16 volume (supports relative and absolute paths).
 */
int fat16_delete_file(const char* filename) {
    if (!g_fat16.is_mounted || !filename) {
        return -1;
    }

    unsigned short parent_cluster = 0;
    char leaf[32];
    if (fat16_resolve_parent_and_leaf(filename, &parent_cluster, leaf, sizeof(leaf)) != 0) {
        return -1;
    }

    char target_name[8];
    char target_ext[3];
    format_to_8_3(leaf, target_name, target_ext);

    fat16_entry_t entry;
    unsigned int sector_lba = 0;
    int slot_idx = 0;

    if (fat16_find_in_dir(parent_cluster, target_name, target_ext, &entry, &sector_lba, &slot_idx) != 0) {
        return -3; // File not found
    }

    if (entry.attributes & FAT_ATTR_DIRECTORY) {
        return -4; // Cannot delete directory with file delete
    }

    if (entry.first_cluster >= 2 && entry.first_cluster < 0xFFF8) {
        fat16_free_cluster_chain(entry.first_cluster);
    }

    if (ata_read_sectors(sector_lba, 1, g_sector_buf) != 0) {
        return -5;
    }
    fat16_raw_entry_t* dir = (fat16_raw_entry_t*)g_sector_buf;
    dir[slot_idx].name[0] = (char)0xE5;

    if (ata_write_sectors(sector_lba, 1, g_sector_buf) != 0) {
        return -5;
    }

    g_cached_fat_lba = 0xFFFFFFFF;
    return 0;
}

/**
 * Creates a new directory at the specified path.
 */
int fat16_mkdir(const char* path) {
    if (!g_fat16.is_mounted || !path) {
        return -1;
    }

    unsigned short parent_cluster = 0;
    char leaf[32];
    if (fat16_resolve_parent_and_leaf(path, &parent_cluster, leaf, sizeof(leaf)) != 0) {
        return -1;
    }

    if (leaf[0] == '\0') {
        return -2;
    }

    char target_name[8];
    char target_ext[3];
    format_to_8_3(leaf, target_name, target_ext);

    // 1. Check if name already exists in parent_cluster
    if (fat16_find_in_dir(parent_cluster, target_name, target_ext, (void*)0, (void*)0, (void*)0) == 0) {
        return -3; // Already exists
    }

    // 2. Allocate a cluster for the new directory
    unsigned short new_cluster = fat16_find_free_cluster();
    if (new_cluster == 0xFFFF) {
        return -4; // Disk full
    }
    fat16_set_cluster_entry(new_cluster, 0xFFFF);

    // 3. Initialize the new directory cluster with '.' and '..'
    memset(g_sector_buf, 0, sizeof(g_sector_buf));
    fat16_raw_entry_t* dir = (fat16_raw_entry_t*)g_sector_buf;

    // Entry 0: '.'
    memcpy(dir[0].name, ".       ", 8);
    memcpy(dir[0].ext, "   ", 3);
    dir[0].attributes = FAT_ATTR_DIRECTORY;
    dir[0].first_cluster_low = new_cluster;
    dir[0].file_size = 0;

    // Entry 1: '..'
    memcpy(dir[1].name, "..      ", 8);
    memcpy(dir[1].ext, "   ", 3);
    dir[1].attributes = FAT_ATTR_DIRECTORY;
    dir[1].first_cluster_low = parent_cluster;
    dir[1].file_size = 0;

    unsigned int cluster_lba = g_fat16.data_lba + (new_cluster - 2) * g_fat16.sectors_per_cluster;
    if (ata_write_sectors(cluster_lba, 1, g_sector_buf) != 0) {
        fat16_set_cluster_entry(new_cluster, 0x0000);
        return -5;
    }

    // Zero out remaining sectors of the cluster
    memset(g_sector_buf, 0, sizeof(g_sector_buf));
    for (unsigned char s = 1; s < g_fat16.sectors_per_cluster; s++) {
        ata_write_sectors(cluster_lba + s, 1, g_sector_buf);
    }

    // 4. Find free slot in parent_cluster and add directory entry
    fat16_dir_iter_t iter;
    fat16_dir_iter_init(&iter, parent_cluster);
    unsigned int lba = 0;
    int slot_found = 0;
    unsigned int free_sector = 0;
    int free_slot = 0;

    while ((lba = fat16_dir_iter_next_sector(&iter)) != 0) {
        if (ata_read_sectors(lba, 1, g_sector_buf) != 0) {
            return -5;
        }
        fat16_raw_entry_t* entries = (fat16_raw_entry_t*)g_sector_buf;
        for (int i = 0; i < 16; i++) {
            unsigned char fb = (unsigned char)entries[i].name[0];
            if (fb == 0x00 || fb == 0xE5) {
                free_sector = lba;
                free_slot = i;
                slot_found = 1;
                break;
            }
        }
        if (slot_found) break;
    }

    if (!slot_found) {
        fat16_set_cluster_entry(new_cluster, 0x0000);
        return -6;
    }

    if (ata_read_sectors(free_sector, 1, g_sector_buf) != 0) {
        return -5;
    }
    dir = (fat16_raw_entry_t*)g_sector_buf;
    memcpy(dir[free_slot].name, target_name, 8);
    memcpy(dir[free_slot].ext, target_ext, 3);
    dir[free_slot].attributes = FAT_ATTR_DIRECTORY;
    dir[free_slot].reserved = 0;
    dir[free_slot].first_cluster_high = 0;
    dir[free_slot].first_cluster_low = new_cluster;
    dir[free_slot].file_size = 0;

    if (ata_write_sectors(free_sector, 1, g_sector_buf) != 0) {
        return -5;
    }

    g_cached_fat_lba = 0xFFFFFFFF;
    return 0;
}

/**
 * Removes an empty directory at the specified path.
 */
int fat16_rmdir(const char* path) {
    if (!g_fat16.is_mounted || !path) {
        return -1;
    }

    unsigned short parent_cluster = 0;
    char leaf[32];
    if (fat16_resolve_parent_and_leaf(path, &parent_cluster, leaf, sizeof(leaf)) != 0) {
        return -1;
    }

    if (leaf[0] == '\0') {
        return -2;
    }

    char target_name[8];
    char target_ext[3];
    format_to_8_3(leaf, target_name, target_ext);

    fat16_entry_t entry;
    unsigned int sector_lba = 0;
    int slot_idx = 0;

    if (fat16_find_in_dir(parent_cluster, target_name, target_ext, &entry, &sector_lba, &slot_idx) != 0) {
        return -3; // Directory not found
    }

    if (!(entry.attributes & FAT_ATTR_DIRECTORY)) {
        return -4; // Not a directory
    }

    unsigned short dir_cluster = entry.first_cluster;
    if (dir_cluster < 2) {
        return -5; // Cannot delete root
    }

    // Check if directory is empty (only '.' and '..' allowed)
    fat16_dir_iter_t iter;
    fat16_dir_iter_init(&iter, dir_cluster);
    unsigned int lba;
    while ((lba = fat16_dir_iter_next_sector(&iter)) != 0) {
        if (ata_read_sectors(lba, 1, g_sector_buf) != 0) {
            return -1;
        }
        fat16_raw_entry_t* entries = (fat16_raw_entry_t*)g_sector_buf;
        for (int i = 0; i < 16; i++) {
            unsigned char fb = (unsigned char)entries[i].name[0];
            if (fb == 0x00) break;
            if (fb == 0xE5) continue;
            if (strncmp(entries[i].name, ".       ", 8) == 0 && strncmp(entries[i].ext, "   ", 3) == 0) continue;
            if (strncmp(entries[i].name, "..      ", 8) == 0 && strncmp(entries[i].ext, "   ", 3) == 0) continue;
            return -6; // Directory not empty!
        }
    }

    // Free the subdirectory cluster chain
    fat16_free_cluster_chain(dir_cluster);

    // Mark directory entry in parent as deleted
    if (ata_read_sectors(sector_lba, 1, g_sector_buf) != 0) {
        return -1;
    }
    fat16_raw_entry_t* dir = (fat16_raw_entry_t*)g_sector_buf;
    dir[slot_idx].name[0] = (char)0xE5;

    if (ata_write_sectors(sector_lba, 1, g_sector_buf) != 0) {
        return -1;
    }

    g_cached_fat_lba = 0xFFFFFFFF;
    return 0;
}

/**
 * Changes current working directory.
 */
int fat16_chdir(const char* path) {
    if (!g_fat16.is_mounted) return -1;
    if (!path || path[0] == '\0') {
        g_cwd_cluster = 0;
        g_cwd_path[0] = '\\';
        g_cwd_path[1] = '\0';
        return 0;
    }

    unsigned short target_cluster = 0;
    if (fat16_traverse_path(path, &target_cluster) != 0) {
        return -1; // Directory not found
    }

    g_cwd_cluster = target_cluster;

    // Update g_cwd_path string
    if (target_cluster == 0) {
        g_cwd_path[0] = '\\';
        g_cwd_path[1] = '\0';
        return 0;
    }

    const char* p = path;
    if ((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) {
        if (p[1] == ':') p += 2;
    }

    if (*p == '/' || *p == '\\') {
        // Absolute path: reconstruct from path
        int pos = 0;
        g_cwd_path[pos++] = '\\';
        while (*p == '/' || *p == '\\') p++;
        while (*p != '\0' && pos < 126) {
            char c = *p++;
            if (c == '/' || c == '\\') {
                if (g_cwd_path[pos - 1] != '\\') g_cwd_path[pos++] = '\\';
                while (*p == '/' || *p == '\\') p++;
            } else {
                g_cwd_path[pos++] = to_upper(c);
            }
        }
        if (pos > 1 && g_cwd_path[pos - 1] == '\\') pos--;
        g_cwd_path[pos] = '\0';
    } else {
        // Relative path: process components ("..", "DIR", etc.)
        while (*p != '\0') {
            char seg[16];
            int seg_len = 0;
            while (*p != '\0' && *p != '/' && *p != '\\' && seg_len < 15) {
                seg[seg_len++] = *p++;
            }
            seg[seg_len] = '\0';
            while (*p == '/' || *p == '\\') p++;

            if (seg_len == 0 || (seg_len == 1 && seg[0] == '.')) {
                continue;
            }

            if (seg_len == 2 && seg[0] == '.' && seg[1] == '.') {
                // Remove last segment from g_cwd_path
                int cur_len = strlen(g_cwd_path);
                int last_slash = -1;
                for (int i = 0; i < cur_len; i++) {
                    if (g_cwd_path[i] == '\\') last_slash = i;
                }
                if (last_slash <= 0) {
                    g_cwd_path[0] = '\\';
                    g_cwd_path[1] = '\0';
                } else {
                    g_cwd_path[last_slash] = '\0';
                }
            } else {
                // Append segment
                int cur_len = strlen(g_cwd_path);
                if (cur_len > 1 && cur_len < 125) {
                    g_cwd_path[cur_len++] = '\\';
                }
                for (int i = 0; i < seg_len && cur_len < 126; i++) {
                    g_cwd_path[cur_len++] = to_upper(seg[i]);
                }
                g_cwd_path[cur_len] = '\0';
            }
        }
    }

    return 0;
}

int fat16_getcwd(char* buffer, unsigned int max_bytes) {
    if (!buffer || max_bytes == 0) return -1;
    unsigned int len = strlen(g_cwd_path);
    if (len >= max_bytes) len = max_bytes - 1;
    memcpy(buffer, g_cwd_path, len);
    buffer[len] = '\0';
    return (int)len;
}

unsigned short fat16_get_cwd_cluster(void) {
    return g_cwd_cluster;
}

const char* fat16_get_cwd_path(void) {
    return g_cwd_path;
}
