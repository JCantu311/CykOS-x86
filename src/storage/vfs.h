#ifndef VFS_H
#define VFS_H

typedef struct vfs_driver vfs_driver_t;

typedef struct {
    char name[13];
    unsigned int size;
    unsigned int first_cluster;
    unsigned char attributes;
    unsigned char volume_index;
    const vfs_driver_t* driver;
} vfs_file_t;

typedef void (*vfs_dir_callback_t)(const char* name, unsigned int size, unsigned char is_dir);

struct vfs_driver {
    const char* name;
    int (*mount_root)(void);
    int (*find_file)(const char* path, vfs_file_t* out_file);
    int (*read_at)(const vfs_file_t* file, unsigned int offset, void* buffer, unsigned int length);
    int (*write_file)(const char* path, const void* data, unsigned int length);
    int (*delete_file)(const char* path);
    int (*list_directory)(const char* path, vfs_dir_callback_t callback);
    int (*mkdir)(const char* path);
    int (*rmdir)(const char* path);
    int (*chdir)(const char* path);
    int (*getcwd)(char* buffer, unsigned int capacity);
};

int vfs_register_driver(const vfs_driver_t* driver);
int vfs_mount_root(void);
const char* vfs_current_driver(void);
int vfs_find_file(const char* path, vfs_file_t* out_file);
int vfs_read_file_by_name(const char* path, void* buffer, unsigned int max_bytes);
int vfs_read_at(const vfs_file_t* file, unsigned int offset, void* buffer, unsigned int length);
int vfs_write_file(const char* path, const void* data, unsigned int length);
int vfs_delete_file(const char* path);
int vfs_list_directory(const char* path, vfs_dir_callback_t callback);
int vfs_mkdir(const char* path);
int vfs_rmdir(const char* path);
int vfs_chdir(const char* path);
int vfs_getcwd(char* buffer, unsigned int capacity);

#endif