#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdarg.h>
#include "syscall.h"

#define SYS_EXIT      1
#define SYS_READ      3
#define SYS_WRITE     4
#define SYS_OPEN      5
#define SYS_CLOSE     6
#define SYS_UNLINK    10
#define SYS_CHDIR     12
#define SYS_LSEEK     19
#define SYS_MKDIR     39
#define SYS_RMDIR     40
#define SYS_BRK       45
#define SYS_SLEEP     104
#define SYS_STAT      108
#define SYS_FSTAT     109
#define SYS_SBRK      112
#define SYS_GETCWD    183

ssize_t read(int fd, void* buf, size_t count) {
    int ret = __syscall3(SYS_READ, fd, (int)buf, (int)count);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}

ssize_t write(int fd, const void* buf, size_t count) {
    int ret = __syscall3(SYS_WRITE, fd, (int)buf, (int)count);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}

int open(const char* pathname, int flags, ...) {
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, mode_t);
        va_end(ap);
    }
    int ret = __syscall3(SYS_OPEN, (int)pathname, flags, (int)mode);
    if (ret < 0) {
        errno = ENOENT;
        return -1;
    }
    return ret;
}

int close(int fd) {
    int ret = __syscall1(SYS_CLOSE, fd);
    if (ret < 0) {
        errno = EBADF;
        return -1;
    }
    return 0;
}

off_t lseek(int fd, off_t offset, int whence) {
    int ret = __syscall3(SYS_LSEEK, fd, (int)offset, whence);
    if (ret < 0) {
        errno = ESPIPE;
        return (off_t)-1;
    }
    return (off_t)ret;
}

int unlink(const char* pathname) {
    int ret = __syscall1(SYS_UNLINK, (int)pathname);
    if (ret < 0) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

int rmdir(const char* pathname) {
    int ret = __syscall1(SYS_RMDIR, (int)pathname);
    if (ret < 0) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

int chdir(const char* path) {
    int ret = __syscall1(SYS_CHDIR, (int)path);
    if (ret < 0) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

char* getcwd(char* buf, size_t size) {
    int ret = __syscall2(SYS_GETCWD, (int)buf, (int)size);
    if (ret < 0) {
        errno = ERANGE;
        return NULL;
    }
    return (char*)ret;
}

int mkdir(const char* pathname, mode_t mode) {
    int ret = __syscall2(SYS_MKDIR, (int)pathname, (int)mode);
    if (ret < 0) {
        errno = EEXIST;
        return -1;
    }
    return 0;
}

int stat(const char* path, struct stat* buf) {
    int ret = __syscall2(SYS_STAT, (int)path, (int)buf);
    if (ret < 0) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

int fstat(int fd, struct stat* buf) {
    int ret = __syscall2(SYS_FSTAT, fd, (int)buf);
    if (ret < 0) {
        errno = EBADF;
        return -1;
    }
    return 0;
}

void* sbrk(intptr_t increment) {
    int ret = __syscall1(SYS_SBRK, (int)increment);
    if (ret == -1) {
        errno = ENOMEM;
        return (void*)-1;
    }
    return (void*)ret;
}

int brk(void* addr) {
    int ret = __syscall1(SYS_BRK, (int)addr);
    if (ret == -1) {
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

unsigned int sleep(unsigned int seconds) {
    __syscall1(SYS_SLEEP, (int)seconds);
    return 0;
}

int isatty(int fd) {
    return (fd >= 0 && fd <= 2) ? 1 : 0;
}
