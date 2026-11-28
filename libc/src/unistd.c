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
#define SYS_GETUID    24
#define SYS_GETPID    20
#define SYS_GETGID    47
#define SYS_SETUID    23
#define SYS_SETGID    46
#define SYS_CHMOD     15
#define SYS_ACCESS    33
#define SYS_UMASK     60
#define SYS_CHOWN     182

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

int lstat(const char* path, struct stat* buf) {
    return stat(path, buf);
}

uid_t getuid(void) { return (uid_t)__syscall0(SYS_GETUID); }
uid_t geteuid(void) { return getuid(); }
gid_t getgid(void) { return (gid_t)__syscall0(SYS_GETGID); }
gid_t getegid(void) { return getgid(); }

int setuid(uid_t uid) {
    int ret = __syscall1(SYS_SETUID, (int)uid);
    if (ret < 0) { errno = EPERM; return -1; }
    return 0;
}

int setgid(gid_t gid) {
    int ret = __syscall1(SYS_SETGID, (int)gid);
    if (ret < 0) { errno = EPERM; return -1; }
    return 0;
}

int chmod(const char* path, mode_t mode) {
    int ret = __syscall2(SYS_CHMOD, (int)path, (int)mode);
    if (ret < 0) { errno = EPERM; return -1; }
    return 0;
}

int fchmod(int fd, mode_t mode) {
    (void)mode;
    if (fd < 0) { errno = EBADF; return -1; }
    return 0;
}

int fchown(int fd, uid_t owner, gid_t group) {
    (void)owner; (void)group;
    if (fd < 0) { errno = EBADF; return -1; }
    return 0;
}

mode_t umask(mode_t mask) {
    return (mode_t)__syscall1(SYS_UMASK, (int)mask);
}

int chown(const char* path, uid_t owner, gid_t group) {
    int ret = __syscall3(SYS_CHOWN, (int)path, (int)owner, (int)group);
    if (ret < 0) { errno = EPERM; return -1; }
    return 0;
}

int access(const char* path, int mode) {
    int ret = __syscall2(SYS_ACCESS, (int)path, mode);
    if (ret < 0) { errno = EACCES; return -1; }
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

int usleep(unsigned int usec) {
    unsigned int sec = (usec + 999999u) / 1000000u;
    if (usec > 0 && sec > 0) {
        __syscall1(SYS_SLEEP, (int)sec);
    }
    return 0;
}

unsigned int alarm(unsigned int seconds) {
    (void)seconds;
    return 0;
}

int getpagesize(void) {
    return 4096;
}

ssize_t readlink(const char* pathname, char* buf, size_t bufsiz) {
    (void)pathname; (void)buf; (void)bufsiz;
    errno = EINVAL;
    return -1;
}

int symlink(const char* target, const char* linkpath) {
    (void)target; (void)linkpath;
    errno = ENOSYS;
    return -1;
}

int link(const char* oldpath, const char* newpath) {
    (void)oldpath; (void)newpath;
    errno = ENOSYS;
    return -1;
}

int isatty(int fd) {
    return (fd >= 0 && fd <= 2) ? 1 : 0;
}

#define SYS_FORK   2
#define SYS_EXECVE 11
#define SYS_DUP    41
#define SYS_PIPE   42
#define SYS_DUP2   63
#define SYS_WAITPID 114

pid_t getpid(void) {
    return (pid_t)__syscall0(SYS_GETPID);
}

void _exit(int status) {
    __syscall1(SYS_EXIT, status);
    for (;;) asm volatile("hlt");
}

pid_t fork(void) {
    int ret = __syscall0(SYS_FORK);
    if (ret < 0) { errno = EAGAIN; return -1; }
    return (pid_t)ret;
}

int execve(const char* pathname, char* const argv[], char* const envp[]) {
    int ret = __syscall3(SYS_EXECVE, (int)pathname, (int)argv, (int)envp);
    if (ret < 0) { errno = ENOENT; return -1; }
    return ret;
}

int execv(const char* pathname, char* const argv[]) {
    return execve(pathname, argv, environ);
}

int execvp(const char* file, char* const argv[]) {
    if (!file || !file[0]) { errno = ENOENT; return -1; }
    if (strchr(file, '/')) {
        return execve(file, argv, environ);
    }
    const char* path_env = getenv("PATH");
    if (!path_env) path_env = "/USR/BIN:/BIN:/usr/bin:/bin";
    char candidate[256];
    const char* p = path_env;
    while (*p) {
        const char* colon = strchr(p, ':');
        size_t dir_len = colon ? (size_t)(colon - p) : strlen(p);
        size_t file_len = strlen(file);
        if (dir_len > 0 && dir_len + 1 + file_len < sizeof(candidate)) {
            memcpy(candidate, p, dir_len);
            candidate[dir_len] = '/';
            memcpy(candidate + dir_len + 1, file, file_len + 1);
            execve(candidate, argv, environ);
        }
        if (!colon) break;
        p = colon + 1;
    }
    errno = ENOENT;
    return -1;
}

int execl(const char* pathname, const char* arg, ...) {
    char* argv[64];
    va_list ap;
    va_start(ap, arg);
    int i = 0;
    argv[i++] = (char*)arg;
    while (i < 63 && (argv[i] = va_arg(ap, char*)) != NULL) i++;
    argv[i] = NULL;
    va_end(ap);
    return execve(pathname, argv, environ);
}

int execlp(const char* file, const char* arg, ...) {
    char* argv[64];
    va_list ap;
    va_start(ap, arg);
    int i = 0;
    argv[i++] = (char*)arg;
    while (i < 63 && (argv[i] = va_arg(ap, char*)) != NULL) i++;
    argv[i] = NULL;
    va_end(ap);
    return execvp(file, argv);
}

long pathconf(const char* path, int name) {
    (void)path;
    switch (name) {
        case _PC_PATH_MAX: return 256;
        case _PC_NAME_MAX: return 127;
        case _PC_PIPE_BUF: return 4096;
        default: return 256;
    }
}

long fpathconf(int fd, int name) {
    (void)fd;
    return pathconf("/", name);
}

long sysconf(int name) {
    switch (name) {
        case _SC_PAGESIZE: return 4096;
        case _SC_OPEN_MAX: return 32;
        case _SC_CLK_TCK:  return 100;
        case _SC_ARG_MAX:  return 16384;
        default: return -1;
    }
}

int pipe(int pipefd[2]) {
    int ret = __syscall1(SYS_PIPE, (int)pipefd);
    if (ret < 0) { errno = EMFILE; return -1; }
    return 0;
}

int dup(int oldfd) {
    int ret = __syscall1(SYS_DUP, oldfd);
    if (ret < 0) { errno = EBADF; return -1; }
    return ret;
}

int dup2(int oldfd, int newfd) {
    int ret = __syscall2(SYS_DUP2, oldfd, newfd);
    if (ret < 0) { errno = EBADF; return -1; }
    return ret;
}

pid_t waitpid(pid_t pid, int* status, int options) {
    int ret = __syscall3(SYS_WAITPID, (int)pid, (int)status, options);
    if (ret < 0) { errno = ECHILD; return -1; }
    return (pid_t)ret;
}

pid_t wait(int* status) {
    return waitpid(-1, status, 0);
}
