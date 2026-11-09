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
                return 0; // Successfully mounted from MBR partition 1
            }
        }

        // 2. Check if LBA 0 itself is a raw FAT16 volume (superfloppy mode)
        if (check_bpb_at_lba(0)) {
            return 0;
        }
    }

    // 3. Fallback scan for common partition offsets (e.g., LBA 64, 128, 2048)
    unsigned int scan_offsets[] = {64, 128, 2048, 32};
    for (unsigned int i = 0; i < sizeof(scan_offsets) / sizeof(scan_offsets[0]); i++) {
        if (check_bpb_at_lba(scan_offsets[i])) {
            return 0;
        }
    }

    return -1; // No FAT16 filesystem detected
}

/**
 * Searches the Root Directory for a specific file by name.
 * @param filename  Name of the file (e.g. "KEYBOARD.BIN" or "keyboard.bin")
 * @param out_entry Output entry structure populated if file is found
 * @return          0 on success, -1 if not found
 */
int fat16_find_file(const char* filename, fat16_entry_t* out_entry) {
    if (!g_fat16.is_mounted || !filename || !out_entry) {
        return -1;
    }

    char target_name[8];
    char target_ext[3];
    format_to_8_3(filename, target_name, target_ext);

    for (unsigned int sector = 0; sector < g_fat16.root_dir_sectors; sector++) {
        if (ata_read_sectors(g_fat16.root_dir_lba + sector, 1, g_sector_buf) != 0) {
            return -1;
        }

        fat16_raw_entry_t* entries = (fat16_raw_entry_t*)g_sector_buf;
        for (int i = 0; i < 16; i++) { // 16 entries of 32 bytes per 512-byte sector
            // End of directory marker
            if ((unsigned char)entries[i].name[0] == 0x00) {
                return -1;
            }

            // Deleted entry marker
            if ((unsigned char)entries[i].name[0] == 0xE5) {
                continue;
            }

            // Skip LFN and Volume ID entries
            if (entries[i].attributes == FAT_ATTR_LFN || (entries[i].attributes & FAT_ATTR_VOLUME_ID)) {
                continue;
            }

            // Compare filename and extension
            if (strncmp(entries[i].name, target_name, 8) == 0 &&
                strncmp(entries[i].ext, target_ext, 3) == 0) {
                format_raw_to_string(entries[i].name, entries[i].ext, out_entry->filename);
                out_entry->file_size = entries[i].file_size;
                out_entry->first_cluster = entries[i].first_cluster_low;
                out_entry->attributes = entries[i].attributes;
                return 0;
            }
        }
    }

    return -1;
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
 * Iterates through all root directory entries and triggers the callback for each.
 */
int fat16_list_directory(fat16_dir_callback_t callback) {
    if (!g_fat16.is_mounted || !callback) {
        return -1;
    }

    char display_name[13];

    for (unsigned int sector = 0; sector < g_fat16.root_dir_sectors; sector++) {
        if (ata_read_sectors(g_fat16.root_dir_lba + sector, 1, g_sector_buf) != 0) {
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
 * Supports both creating new files and overwriting existing files.
 * Freestanding: zero libc dependencies.
 */
int fat16_write_file(const char* filename, const void* data, unsigned int size) {
    if (!g_fat16.is_mounted || !filename) {
        return -1;
    }

    char target_name[8];
    char target_ext[3];
    format_to_8_3(filename, target_name, target_ext);

    int entry_found = 0;
    unsigned int found_dir_sector = 0;
    int found_dir_index = 0;
    unsigned short old_first_cluster = 0;

    int free_slot_found = 0;
    unsigned int free_dir_sector = 0;
    int free_dir_index = 0;

    for (unsigned int sector = 0; sector < g_fat16.root_dir_sectors; sector++) {
        unsigned int dir_lba = g_fat16.root_dir_lba + sector;
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
        return -3; // Root directory full
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

    return 0;
}

/**
 * Convenience helper to read a file by its filename directly.
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
 * Deletes a file from the FAT16 volume.
 * Frees its cluster chain and marks the directory entry with 0xE5.
 * Returns 0 on success, negative error code on failure.
 */
int fat16_delete_file(const char* filename) {
    if (!g_fat16.is_mounted || !filename) {
        return -1;
    }

    char target_name[8];
    char target_ext[3];
    format_to_8_3(filename, target_name, target_ext);

    for (unsigned int sector = 0; sector < g_fat16.root_dir_sectors; sector++) {
        unsigned int dir_lba = g_fat16.root_dir_lba + sector;
        if (ata_read_sectors(dir_lba, 1, g_sector_buf) != 0) {
            return -2;
        }

        fat16_raw_entry_t* entries = (fat16_raw_entry_t*)g_sector_buf;
        for (int i = 0; i < 16; i++) {
            unsigned char first_byte = (unsigned char)entries[i].name[0];

            if (first_byte == 0x00) {
                return -3; // End of directory, file not found
            }
            if (first_byte == 0xE5) {
                continue; // Already deleted
            }

            if (entries[i].attributes == FAT_ATTR_LFN || (entries[i].attributes & FAT_ATTR_VOLUME_ID)) {
                continue;
            }

            if (strncmp(entries[i].name, target_name, 8) == 0 &&
                strncmp(entries[i].ext, target_ext, 3) == 0) {

                // Do not delete directories via file delete
                if (entries[i].attributes & FAT_ATTR_DIRECTORY) {
                    return -4;
                }

                // Free the cluster chain if any clusters were allocated
                unsigned short start_cluster = entries[i].first_cluster_low;
                if (start_cluster >= 2 && start_cluster < 0xFFF8) {
                    fat16_free_cluster_chain(start_cluster);
                }

                // Mark entry as deleted (0xE5)
                entries[i].name[0] = (char)0xE5;

                // Write directory sector back to disk
                if (ata_write_sectors(dir_lba, 1, g_sector_buf) != 0) {
                    return -5;
                }

                g_cached_fat_lba = 0xFFFFFFFF;
                return 0; // Successfully deleted
            }
        }
    }

    return -3; // Not found
}
