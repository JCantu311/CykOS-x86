#ifndef _DIRENT_H
#define _DIRENT_H

#define DT_UNKNOWN 0
#define DT_DIR     4
#define DT_REG     8

struct dirent {
    unsigned int d_ino;
    unsigned char d_type;
    char d_name[128];
};

typedef struct {
    struct dirent* entries;
    int count;
    int pos;
} DIR;

DIR* opendir(const char* name);
struct dirent* readdir(DIR* dirp);
int closedir(DIR* dirp);
void rewinddir(DIR* dirp);

#endif /* _DIRENT_H */
