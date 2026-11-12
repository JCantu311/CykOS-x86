#ifndef FAT16_H
#define FAT16_H

#include "cykastd.h"
#include "vfs.h"

// File attributes
#define FAT_ATTR_READ_ONLY 0x01
#define FAT_ATTR_HIDDEN    0x02
#define FAT_ATTR_SYSTEM    0x04
#define FAT_ATTR_VOLUME_ID 0x08
#define FAT_ATTR_DIRECTORY 0x10
#define FAT_ATTR_ARCHIVE   0x20
#define FAT_ATTR_LFN       0x0F

// Parsed File Entry Structure
typedef struct {
    char filename[13];          // Null-terminated 8.3 name (e.g. "KEYBOARD.BIN")
    unsigned int file_size;     // Size of file in bytes
    unsigned short first_cluster;// First cluster index in data area
    unsigned char attributes;   // File attribute flags
    unsigned char volume_index;  // Mounted volume containing this file
} fat16_entry_t;

// Callback type for directory listing
typedef void (*fat16_dir_callback_t)(const char* name, unsigned int size, unsigned char is_dir);

// Public FAT16 API
int fat16_init(void);
int fat16_mount_extra_drives(void);
int fat16_find_file(const char* filename, fat16_entry_t* out_entry);
int fat16_read_file(const fat16_entry_t* entry, void* buffer, unsigned int max_bytes);
int fat16_read_file_at(const fat16_entry_t* entry, unsigned int offset, void* buffer, unsigned int length);
int fat16_read_file_by_name(const char* filename, void* buffer, unsigned int max_bytes);
int fat16_write_file(const char* filename, const void* data, unsigned int size);
int fat16_delete_file(const char* filename);
int fat16_list_directory(fat16_dir_callback_t callback);
int fat16_list_directory_cluster(unsigned short dir_cluster, fat16_dir_callback_t callback);

// Hierarchical Directory Management
int fat16_mkdir(const char* path);
int fat16_rmdir(const char* path);
int fat16_chdir(const char* path);
int fat16_getcwd(char* buffer, unsigned int max_bytes);
unsigned short fat16_get_cwd_cluster(void);
const char* fat16_get_cwd_path(void);

void fat16_print_volume_info(void);

extern const vfs_driver_t fat16_vfs_driver;

#endif // FAT16_H
