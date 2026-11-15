#ifndef _SYS_STAT_H
#define _SYS_STAT_H

#include <sys/types.h>

struct stat {
    dev_t     st_dev;     /* ID of device containing file */
    uid_t     st_uid;     /* User ID of owner */
    mode_t    st_mode;    /* File type and mode */
    nlink_t   st_nlink;   /* Number of hard links */
    gid_t     st_gid;      /* Group ID of owner */
    dev_t     st_rdev;     /* Device ID (zero for regular files) */
    off_t     st_size;    /* Total size, in bytes */
    unsigned int st_blksize;
    unsigned int st_blocks;
    time_t    st_atime;   /* Time of last access */
    time_t    st_mtime;   /* Time of last modification */
    time_t    st_ctime;   /* Time of last status change */
};

#define S_IFMT   00170000
#define S_IFREG  0100000
#define S_IFDIR  0040000
#define S_IFCHR  0020000
#define S_ISUID  0004000
#define S_ISGID  0002000
#define S_ISVTX  0001000

#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m) (((m) & S_IFMT) == S_IFCHR)

#define S_IRWXU 00700
#define S_IRUSR 00400
#define S_IWUSR 00200
#define S_IXUSR 00100

#define S_IRWXG 00070
#define S_IRGRP 00040
#define S_IWGRP 00020
#define S_IXGRP 00010

#define S_IRWXO 00007
#define S_IROTH 00004
#define S_IWOTH 00002
#define S_IXOTH 00001

int stat(const char* path, struct stat* buf);
int fstat(int fd, struct stat* buf);
int mkdir(const char* pathname, mode_t mode);
int chmod(const char* path, mode_t mode);
int fchmod(int fd, mode_t mode);
int chown(const char* path, uid_t owner, gid_t group);
int fchown(int fd, uid_t owner, gid_t group);
int access(const char* path, int mode);

#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4

#endif
