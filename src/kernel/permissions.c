#include "permissions.h"
#include "vfs.h"
#include "cykastd.h"

#define PERMISSION_ENTRIES 64
#define PERMISSION_DB "/ETC/PERMISSIONS.DB"
#define PERMISSION_MAGIC 0x43595031u

typedef struct {
    unsigned int magic;
    unsigned int count;
} permission_header_t;

typedef struct {
    char path[128];
    unsigned int mode;
    unsigned int uid;
    unsigned int gid;
} permission_entry_t;

static permission_entry_t g_entries[PERMISSION_ENTRIES];
static unsigned int g_count;
static unsigned int g_uid;
static unsigned int g_gid;

static int path_equal(const char* a, const char* b) {
    while (*a && *b) {
        char ca = *a >= 'a' && *a <= 'z' ? (char)(*a - 'a' + 'A') : *a;
        char cb = *b >= 'a' && *b <= 'z' ? (char)(*b - 'a' + 'A') : *b;
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == *b;
}

static int find_entry(const char* path) {
    for (unsigned int i = 0; i < g_count; i++)
        if (path_equal(g_entries[i].path, path)) return (int)i;
    return -1;
}

static int ensure_entry(const char* path, int is_dir) {
    int index = find_entry(path);
    if (index >= 0) return index;
    if (!path || strlen(path) >= sizeof(g_entries[0].path) || g_count >= PERMISSION_ENTRIES) return -1;
    index = (int)g_count++;
    memset(&g_entries[index], 0, sizeof(g_entries[index]));
    memcpy(g_entries[index].path, path, strlen(path) + 1);
    vfs_file_t file;
    if (vfs_find_file(path, &file) == 0) is_dir = (file.attributes & 0x10) != 0;
    g_entries[index].mode = is_dir ? 0040755 : 0100644;
    g_entries[index].uid = 0;
    g_entries[index].gid = 0;
    return index;
}

static void persist(void) {
    unsigned char data[sizeof(permission_header_t) + sizeof(permission_entry_t) * PERMISSION_ENTRIES];
    permission_header_t header = { PERMISSION_MAGIC, g_count };
    unsigned int bytes = sizeof(header) + g_count * sizeof(permission_entry_t);
    memcpy(data, &header, sizeof(header));
    memcpy(data + sizeof(header), g_entries, g_count * sizeof(permission_entry_t));
    (void)vfs_write_file(PERMISSION_DB, data, bytes);
}

void permissions_init(void) {
    permission_header_t header;
    unsigned char data[sizeof(permission_header_t) + sizeof(permission_entry_t) * PERMISSION_ENTRIES];
    g_uid = 0;
    g_gid = 0;
    g_count = 0;
    (void)vfs_mkdir("/ETC");
    int bytes = vfs_read_file_by_name(PERMISSION_DB, data, sizeof(data));
    if (bytes >= (int)sizeof(header)) {
        memcpy(&header, data, sizeof(header));
        if (header.magic == PERMISSION_MAGIC && header.count <= PERMISSION_ENTRIES &&
            (unsigned int)bytes >= sizeof(header) + header.count * sizeof(permission_entry_t)) {
            memcpy(g_entries, data + sizeof(header), header.count * sizeof(permission_entry_t));
            g_count = header.count;
        }
    }
    if (g_count == 0) persist();
}

unsigned int permissions_uid(void) { return g_uid; }
unsigned int permissions_gid(void) { return g_gid; }

int permissions_set_uid(unsigned int uid) {
    if (g_uid != 0 && uid != g_uid) return -1;
    g_uid = uid;
    return 0;
}

int permissions_set_gid(unsigned int gid) {
    if (g_uid != 0 && gid != g_gid) return -1;
    g_gid = gid;
    return 0;
}

int permissions_get(const char* path, unsigned int* mode, unsigned int* uid, unsigned int* gid) {
    vfs_file_t file;
    if (!path || vfs_find_file(path, &file) != 0) return -1;
    int index = ensure_entry(path, (file.attributes & 0x10) != 0);
    if (index < 0) return -1;
    if (mode) *mode = g_entries[index].mode;
    if (uid) *uid = g_entries[index].uid;
    if (gid) *gid = g_entries[index].gid;
    return 0;
}

int permissions_check(const char* path, unsigned int requested) {
    unsigned int mode, uid, gid;
    if (permissions_get(path, &mode, &uid, &gid) != 0) return -1;
    if (g_uid == 0) return ((requested & CYK_PERMISSION_EXEC) && !(mode & 0111)) ? -1 : 0;
    unsigned int shift = g_uid == uid ? 6 : (g_gid == gid ? 3 : 0);
    return (((mode >> shift) & requested) == requested) ? 0 : -1;
}

int permissions_chmod(const char* path, unsigned int mode) {
    if (g_uid != 0) {
        unsigned int owner;
        if (permissions_get(path, 0, &owner, 0) != 0 || owner != g_uid) return -1;
    }
    int index = ensure_entry(path, 0);
    if (index < 0) return -1;
    g_entries[index].mode = (g_entries[index].mode & 0170000) | (mode & 07777);
    persist();
    return 0;
}

int permissions_chown(const char* path, unsigned int uid, unsigned int gid) {
    if (g_uid != 0) return -1;
    int index = ensure_entry(path, 0);
    if (index < 0) return -1;
    g_entries[index].uid = uid;
    g_entries[index].gid = gid;
    persist();
    return 0;
}

int permissions_create(const char* path, unsigned int mode, int is_dir) {
    int index = ensure_entry(path, is_dir);
    if (index < 0) return -1;
    g_entries[index].mode = (is_dir ? 0040000 : 0100000) | (mode & 0777);
    g_entries[index].uid = g_uid;
    g_entries[index].gid = g_gid;
    persist();
    return 0;
}
