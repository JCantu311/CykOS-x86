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
