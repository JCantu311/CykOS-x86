#include "permissions.h"
#include "vfs.h"
#include "cykastd.h"

#define PERMISSION_ENTRIES 128
#define PERMISSION_DB "/ETC/PERMISSIONS.DB"
#define PERMISSION_MAGIC 0x43595031u
#define PERMISSION_PATH_MAX 128

typedef struct {
    unsigned int magic;
    unsigned int count;
} permission_header_t;

typedef struct {
    char path[PERMISSION_PATH_MAX];
    unsigned int mode;
    unsigned int uid;
    unsigned int gid;
} permission_entry_t;

static permission_entry_t* g_entries;
static unsigned int g_count;
static unsigned int g_uid;
static unsigned int g_gid;
static unsigned int g_umask = 0022;
static int g_ready;
static int g_persisting;
static int g_metadata_degraded;

static char permission_upper(char c) {
    return c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c;
}

static int path_equal(const char* a, const char* b) {
    while (*a && *b) {
        if (permission_upper(*a) != permission_upper(*b)) return 0;
        a++; b++;
    }
    return *a == *b;
}

int permissions_normalize_path(const char* path, char output[PERMISSION_PATH_MAX]) {
    if (!path || !output || !path[0]) return -1;

    char joined[256];
    unsigned int length = 0;
    if (path[0] != '/' && path[0] != '\\') {
        char cwd[PERMISSION_PATH_MAX];
        int cwd_length = vfs_getcwd(cwd, sizeof(cwd));
        if (cwd_length < 1 || cwd_length >= (int)sizeof(cwd)) return -1;
        if ((unsigned int)cwd_length + 1u >= sizeof(joined)) return -1;
        memcpy(joined, cwd, (unsigned int)cwd_length);
        length = (unsigned int)cwd_length;
        if (length > 1 && joined[length - 1] != '/') joined[length++] = '/';
    }

    unsigned int path_index = 0;
    while (path[path_index]) {
        if (length >= sizeof(joined) - 1) return -1;
        char c = path[path_index++];
        joined[length++] = c == '\\' ? '/' : c;
    }
    joined[length] = '\0';
    if (joined[0] != '/') return -1;

    unsigned int output_length = 1;
    output[0] = '/';
    unsigned int pos = 0;
    while (pos < length) {
        while (pos < length && joined[pos] == '/') pos++;
        if (pos >= length) break;
        unsigned int start = pos;
        while (pos < length && joined[pos] != '/') pos++;
        unsigned int component_length = pos - start;
        if (component_length == 1 && joined[start] == '.') continue;
        if (component_length == 2 && joined[start] == '.' && joined[start + 1] == '.') {
            while (output_length > 1 && output[output_length - 1] != '/') output_length--;
            if (output_length > 1) output_length--;
            continue;
        }
        unsigned int separator = output_length > 1 ? 1u : 0u;
        if (output_length + separator + component_length >= PERMISSION_PATH_MAX) return -1;
        if (separator) output[output_length++] = '/';
        for (unsigned int i = 0; i < component_length; i++)
            output[output_length++] = permission_upper(joined[start + i]);
    }
    output[output_length] = '\0';
    return 0;
}

static int find_entry(const char* canonical_path) {
    for (unsigned int i = 0; i < g_count; i++)
        if (path_equal(g_entries[i].path, canonical_path)) return (int)i;
    return -1;
}

static int path_is_executable(const char* path) {
    const char* leaf = path;
    for (const char* p = path; *p; p++) if (*p == '/') leaf = p + 1;
    unsigned int length = (unsigned int)strlen(leaf);
    if (length < 4) return 0;
    const char* suffix = leaf + length - 4;
    return path_equal(suffix, ".BIN") || path_equal(suffix, ".COM") ||
           path_equal(suffix, ".SHE");
}

static int ensure_entry(const char* canonical_path, int is_dir) {
    int index = find_entry(canonical_path);
    if (index >= 0) return index;
    if (!canonical_path || strlen(canonical_path) >= PERMISSION_PATH_MAX ||
        g_count >= PERMISSION_ENTRIES) return -1;

    vfs_file_t file;
    if (vfs_find_file(canonical_path, &file) != 0) return -1;
    is_dir = (file.attributes & 0x10) != 0;
    index = (int)g_count++;
    permission_entry_t* entry = &g_entries[index];
    memset(entry, 0, sizeof(*entry));
    memcpy(entry->path, canonical_path, strlen(canonical_path) + 1);
    entry->mode = is_dir ? 0040755u : (path_is_executable(canonical_path) ? 0100755u : 0100644u);
    entry->uid = 0;
    entry->gid = 0;
    return index;
}

static int persist(void) {
    if (!g_ready || !g_entries) return 0;
    unsigned int cap = sizeof(permission_header_t) + sizeof(permission_entry_t) * PERMISSION_ENTRIES;
    unsigned char* data = (unsigned char*)malloc(cap);
    if (!data) return -1;
    permission_header_t header = { PERMISSION_MAGIC, g_count };
    unsigned int bytes = sizeof(header) + g_count * sizeof(permission_entry_t);
    memcpy(data, &header, sizeof(header));
    memcpy(data + sizeof(header), g_entries, g_count * sizeof(permission_entry_t));
    g_persisting = 1;
    int result = vfs_write_file(PERMISSION_DB, data, bytes);
    g_persisting = 0;
    free(data);
    return result;
}

void permissions_init(void) {
    permission_header_t header;
    unsigned int cap = sizeof(permission_header_t) + sizeof(permission_entry_t) * PERMISSION_ENTRIES;
    unsigned char* data = (unsigned char*)malloc(cap);
    if (!data) return;
    g_uid = 0;
    g_gid = 0;
    g_umask = 0022;
    g_count = 0;
    g_ready = 0;
    g_persisting = 0;
    g_metadata_degraded = 0;
    if (!g_entries) {
        g_entries = (permission_entry_t*)calloc(PERMISSION_ENTRIES, sizeof(permission_entry_t));
        if (!g_entries) { free(data); return; }
    } else {
        memset(g_entries, 0, PERMISSION_ENTRIES * sizeof(permission_entry_t));
    }
    (void)vfs_mkdir("/ETC");

    int bytes = vfs_read_file_by_name(PERMISSION_DB, data, cap);
    if (bytes >= (int)sizeof(header)) {
        memcpy(&header, data, sizeof(header));
        if (header.magic == PERMISSION_MAGIC && header.count <= PERMISSION_ENTRIES &&
            (unsigned int)bytes >= sizeof(header) + header.count * sizeof(permission_entry_t)) {
            permission_entry_t* stored = (permission_entry_t*)(data + sizeof(header));
            for (unsigned int i = 0; i < header.count; i++) {
                unsigned int path_length = 0;
                while (path_length < PERMISSION_PATH_MAX && stored[i].path[path_length]) path_length++;
                if (!path_length || path_length == PERMISSION_PATH_MAX) continue;
                char canonical[PERMISSION_PATH_MAX];
                if (permissions_normalize_path(stored[i].path, canonical) != 0 ||
                    !path_equal(stored[i].path, canonical)) continue;
                vfs_file_t file;
                if (vfs_find_file(canonical, &file) != 0) continue;
                if ((stored[i].mode & 0170000u) != 0100000u &&
                    (stored[i].mode & 0170000u) != 0040000u) continue;
                g_entries[g_count] = stored[i];
                g_count++;
            }
        }
    }
    free(data);
    g_ready = 1;
}

int permissions_is_ready(void) { return g_ready; }
int permissions_is_persisting(void) { return g_persisting; }
unsigned int permissions_uid(void) { return g_uid; }
unsigned int permissions_gid(void) { return g_gid; }
unsigned int permissions_umask(void) { return g_umask; }
unsigned int permissions_set_umask(unsigned int mask) {
    unsigned int previous = g_umask;
    g_umask = mask & 0777u;
    return previous;
}

void permissions_set_identity(unsigned int uid, unsigned int gid) {
    g_uid = uid;
    g_gid = gid;
}

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
    char canonical[PERMISSION_PATH_MAX];
    vfs_file_t file;
    if (permissions_normalize_path(path, canonical) != 0 || vfs_find_file(canonical, &file) != 0) return -1;
    int index = find_entry(canonical);
    if (index >= 0) {
        if (mode) *mode = g_entries[index].mode;
        if (uid) *uid = g_entries[index].uid;
        if (gid) *gid = g_entries[index].gid;
        return 0;
    }
    int is_dir = (file.attributes & 0x10u) != 0;
    if (mode) *mode = is_dir ? 0040755u : (path_is_executable(canonical) ? 0100755u : 0100644u);
    if (uid) *uid = 0;
    if (gid) *gid = 0;
    return 0;
}

static int mode_allows(unsigned int mode, unsigned int owner, unsigned int group,
                       unsigned int requested) {
    if (g_uid == 0) return !(requested & CYK_PERMISSION_EXEC) || (mode & 0111u);
    unsigned int shift = g_uid == owner ? 6u : (g_gid == group ? 3u : 0u);
    return (((mode >> shift) & requested) == requested);
}

int permissions_check(const char* path, unsigned int requested) {
    if (requested & ~(CYK_PERMISSION_READ | CYK_PERMISSION_WRITE | CYK_PERMISSION_EXEC)) return -1;
    if (!g_ready) return 0;
    if (g_metadata_degraded && g_uid != 0) return -1;
    char canonical[PERMISSION_PATH_MAX];
    if (permissions_normalize_path(path, canonical) != 0) return -1;

    /* Every parent directory must grant search permission. */
    if (g_uid != 0) {
        for (unsigned int i = 1; canonical[i]; i++) {
            if (canonical[i] != '/') continue;
            char ancestor[PERMISSION_PATH_MAX];
            memcpy(ancestor, canonical, i);
            ancestor[i] = '\0';
            unsigned int mode, owner, group;
            if (permissions_get(ancestor, &mode, &owner, &group) != 0 ||
                !mode_allows(mode, owner, group, CYK_PERMISSION_EXEC)) return -1;
        }
    }
    unsigned int mode, owner, group;
    if (permissions_get(canonical, &mode, &owner, &group) != 0) return -1;
    return mode_allows(mode, owner, group, requested) ? 0 : -1;
}

int permissions_check_parent(const char* path, unsigned int requested) {
    char canonical[PERMISSION_PATH_MAX];
    if (permissions_normalize_path(path, canonical) != 0) return -1;
    unsigned int length = (unsigned int)strlen(canonical);
    while (length > 0 && canonical[length - 1] != '/') length--;
    if (length == 0) return -1;
    if (length == 1) canonical[1] = '\0';
    else canonical[length - 1] = '\0';
    return permissions_check(canonical, requested);
}

int permissions_chmod(const char* path, unsigned int mode) {
    char canonical[PERMISSION_PATH_MAX];
    unsigned int old_mode, uid, gid;
    if (permissions_normalize_path(path, canonical) != 0 ||
        permissions_check(canonical, 0) != 0 ||
        permissions_get(canonical, &old_mode, &uid, &gid) != 0) return -1;
    if (g_uid != 0 && uid != g_uid) return -1;
    int index = ensure_entry(canonical, 0);
    if (index < 0) return -1;
    unsigned int previous = g_entries[index].mode;
    g_entries[index].mode = (previous & 0170000u) | (mode & 07777u);
    if (persist() != 0) {
        g_entries[index].mode = previous;
        g_metadata_degraded = 1;
        return -1;
    }
    return 0;
}

int permissions_chown(const char* path, unsigned int uid, unsigned int gid) {
    char canonical[PERMISSION_PATH_MAX];
    if (g_uid != 0 || permissions_normalize_path(path, canonical) != 0 ||
        permissions_check(canonical, 0) != 0 || permissions_get(canonical, 0, 0, 0) != 0) return -1;
    int index = ensure_entry(canonical, 0);
    if (index < 0) return -1;
    unsigned int old_uid = g_entries[index].uid;
    unsigned int old_gid = g_entries[index].gid;
    g_entries[index].uid = uid;
    g_entries[index].gid = gid;
    if (persist() != 0) {
        g_entries[index].uid = old_uid;
        g_entries[index].gid = old_gid;
        g_metadata_degraded = 1;
        return -1;
    }
    return 0;
}

int permissions_create(const char* path, unsigned int mode, int is_dir) {
    char canonical[PERMISSION_PATH_MAX];
    if (permissions_normalize_path(path, canonical) != 0) return -1;
    if (path_equal(canonical, PERMISSION_DB)) return 0;
    int index = find_entry(canonical);
    unsigned int previous_count = g_count;
    permission_entry_t previous;
    if (index < 0) {
        if (g_count >= PERMISSION_ENTRIES) return -1;
        index = (int)g_count++;
        memset(&g_entries[index], 0, sizeof(g_entries[index]));
        memcpy(g_entries[index].path, canonical, strlen(canonical) + 1);
    } else {
        previous = g_entries[index];
    }
    g_entries[index].mode = (is_dir ? 0040000u : 0100000u) |
                            ((mode & 0777u) & ~g_umask);
    g_entries[index].uid = g_uid;
    g_entries[index].gid = g_gid;
    if (persist() != 0) {
        if (previous_count == g_count) g_entries[index] = previous;
        else {
            memset(&g_entries[index], 0, sizeof(g_entries[index]));
            g_count = previous_count;
        }
        g_metadata_degraded = 1;
        return -1;
    }
    return 0;
}

int permissions_remove(const char* path) {
    char canonical[PERMISSION_PATH_MAX];
    if (permissions_normalize_path(path, canonical) != 0) return -1;
    if (path_equal(canonical, PERMISSION_DB)) return 0;
    int index = find_entry(canonical);
    if (index < 0) return 0;
    permission_entry_t removed = g_entries[index];
    for (unsigned int i = (unsigned int)index + 1; i < g_count; i++)
        g_entries[i - 1] = g_entries[i];
    g_count--;
    memset(&g_entries[g_count], 0, sizeof(g_entries[g_count]));
    if (persist() != 0) {
        for (unsigned int i = g_count; i > (unsigned int)index; i--)
            g_entries[i] = g_entries[i - 1];
        g_entries[index] = removed;
        g_count++;
        g_metadata_degraded = 1;
        return -1;
    }
    return 0;
}
