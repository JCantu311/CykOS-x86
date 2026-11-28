#include <dirent.h>
#include <stdlib.h>
#include <errno.h>
#include "syscall.h"

#define SYS_LISTDIR 213
#define DIR_MAX_ENTRIES 128

DIR* opendir(const char* name) {
    if (!name || !name[0]) {
        errno = ENOENT;
        return (void*)0;
    }
    struct dirent* buf = (struct dirent*)calloc(DIR_MAX_ENTRIES, sizeof(struct dirent));
    if (!buf) {
        errno = ENOMEM;
        return (void*)0;
    }
    int count = __syscall3(SYS_LISTDIR, (int)name, (int)buf, DIR_MAX_ENTRIES);
    if (count < 0) {
        free(buf);
        errno = ENOENT;
        return (void*)0;
    }
    DIR* d = (DIR*)malloc(sizeof(DIR));
    if (!d) {
        free(buf);
        errno = ENOMEM;
        return (void*)0;
    }
    d->entries = buf;
    d->count = count;
    d->pos = 0;
    return d;
}

struct dirent* readdir(DIR* dirp) {
    if (!dirp || !dirp->entries || dirp->pos >= dirp->count) {
        return (void*)0;
    }
    return &dirp->entries[dirp->pos++];
}

int closedir(DIR* dirp) {
    if (!dirp) {
        errno = EBADF;
        return -1;
    }
    if (dirp->entries) free(dirp->entries);
    free(dirp);
    return 0;
}

void rewinddir(DIR* dirp) {
    if (dirp) dirp->pos = 0;
}
