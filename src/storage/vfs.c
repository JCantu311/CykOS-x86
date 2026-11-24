#include "vfs.h"
#include "permissions.h"

#define VFS_MAX_DRIVERS 8

extern const vfs_driver_t fat16_vfs_driver;
extern const vfs_driver_t fat32_vfs_driver;
extern const vfs_driver_t ext2_vfs_driver;

static const vfs_driver_t* g_drivers[VFS_MAX_DRIVERS];
static unsigned int g_driver_count;
static const vfs_driver_t* g_root_driver;

int vfs_register_driver(const vfs_driver_t* driver) {
    if (!driver || !driver->name || !driver->mount_root || g_driver_count >= VFS_MAX_DRIVERS) return -1;
    for (unsigned int i = 0; i < g_driver_count; i++) {
        if (g_drivers[i] == driver) return 0;
    }
    g_drivers[g_driver_count++] = driver;
    return 0;
}

int vfs_mount_root(void) {
    g_root_driver = 0;
    if (vfs_register_driver(&ext2_vfs_driver) != 0 ||
        vfs_register_driver(&fat16_vfs_driver) != 0 ||
        vfs_register_driver(&fat32_vfs_driver) != 0) return -1;
    for (unsigned int i = 0; i < g_driver_count; i++) {
        if (g_drivers[i]->mount_root() == 0) {
            g_root_driver = g_drivers[i];
            return 0;
        }
    }
    return -1;
}

const char* vfs_current_driver(void) {
    return g_root_driver ? g_root_driver->name : "none";
}

int vfs_find_file(const char* path, vfs_file_t* out_file) {
    if (!g_root_driver || !g_root_driver->find_file || !out_file) return -1;
    int result = g_root_driver->find_file(path, out_file);
    if (result == 0) out_file->driver = g_root_driver;
    return result;
}

int vfs_read_at(const vfs_file_t* file, unsigned int offset, void* buffer, unsigned int length) {
    if (!file || !file->driver || !file->driver->read_at) return -1;
    return file->driver->read_at(file, offset, buffer, length);
}

int vfs_read_file_by_name(const char* path, void* buffer, unsigned int max_bytes) {
    vfs_file_t file;
    if (!buffer || (permissions_is_ready() &&
        permissions_check(path, CYK_PERMISSION_READ) != 0) || vfs_find_file(path, &file) != 0) return -1;
    return vfs_read_at(&file, 0, buffer, max_bytes);
}

int vfs_write_file(const char* path, const void* data, unsigned int length) {
    if (!g_root_driver || !g_root_driver->write_file) return -1;
    int privileged_metadata_write = permissions_is_persisting();
    vfs_file_t existing;
    int was_present = g_root_driver->find_file && g_root_driver->find_file(path, &existing) == 0;
    if (!privileged_metadata_write && permissions_is_ready()) {
        int allowed = was_present
            ? permissions_check(path, CYK_PERMISSION_WRITE)
            : permissions_check_parent(path, CYK_PERMISSION_WRITE | CYK_PERMISSION_EXEC);
        if (allowed != 0) return -1;
    }
    if (g_root_driver->write_file(path, data, length) != 0) return -1;
    if (!privileged_metadata_write && !was_present && permissions_is_ready() &&
        permissions_create(path, 0666u, 0) != 0) {
        if (g_root_driver->delete_file) (void)g_root_driver->delete_file(path);
        return -1;
    }
    return 0;
}

int vfs_delete_file(const char* path) {
    if (!g_root_driver || !g_root_driver->delete_file) return -1;
    if (permissions_is_ready() &&
        permissions_check_parent(path, CYK_PERMISSION_WRITE | CYK_PERMISSION_EXEC) != 0) return -1;
    if (g_root_driver->delete_file(path) != 0) return -1;
    return !permissions_is_ready() || permissions_remove(path) == 0 ? 0 : -1;
}

int vfs_list_directory(const char* path, vfs_dir_callback_t callback) {
    if (!g_root_driver || !g_root_driver->list_directory || !callback) return -1;
    if (permissions_is_ready() && permissions_check(path && path[0] ? path : ".",
            CYK_PERMISSION_READ | CYK_PERMISSION_EXEC) != 0) return -1;
    return g_root_driver->list_directory(path, callback);
}

int vfs_mkdir(const char* path) {
    if (!g_root_driver || !g_root_driver->mkdir) return -1;
    if (permissions_is_ready() &&
        permissions_check_parent(path, CYK_PERMISSION_WRITE | CYK_PERMISSION_EXEC) != 0) return -1;
    if (g_root_driver->mkdir(path) != 0) return -1;
    if (permissions_is_ready() && permissions_create(path, 0777u, 1) != 0) {
        if (g_root_driver->rmdir) (void)g_root_driver->rmdir(path);
        return -1;
    }
    return 0;
}

int vfs_rmdir(const char* path) {
    if (!g_root_driver || !g_root_driver->rmdir) return -1;
    if (permissions_is_ready() &&
        permissions_check_parent(path, CYK_PERMISSION_WRITE | CYK_PERMISSION_EXEC) != 0) return -1;
    if (g_root_driver->rmdir(path) != 0) return -1;
    return !permissions_is_ready() || permissions_remove(path) == 0 ? 0 : -1;
}

int vfs_chdir(const char* path) {
    if (!g_root_driver || !g_root_driver->chdir) return -1;
    if (permissions_is_ready() && permissions_check(path, CYK_PERMISSION_EXEC) != 0) return -1;
    return g_root_driver->chdir(path);
}

int vfs_getcwd(char* buffer, unsigned int capacity) {
    if (!g_root_driver || !g_root_driver->getcwd) return -1;
    return g_root_driver->getcwd(buffer, capacity);
}
