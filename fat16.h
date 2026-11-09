#ifndef FAT16_H
#define FAT16_H

#include "cykastd.h"

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
} fat16_entry_t;

// Callback type for directory listing
typedef void (*fat16_dir_callback_t)(const char* name, unsigned int size, unsigned char is_dir);

// Public FAT16 API
int fat16_init(void);
int fat16_find_file(const char* filename, fat16_entry_t* out_entry);
int fat16_read_file(const fat16_entry_t* entry, void* buffer, unsigned int max_bytes);
int fat16_read_file_by_name(const char* filename, void* buffer, unsigned int max_bytes);
int fat16_write_file(const char* filename, const void* data, unsigned int size);
int fat16_list_directory(fat16_dir_callback_t callback);
void fat16_print_volume_info(void);

#endif // FAT16_H
