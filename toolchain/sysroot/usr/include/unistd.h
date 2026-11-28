#ifndef _UNISTD_H
#define _UNISTD_H

#include "sys/types.h"
#include "stdint.h"
#include "sys/stat.h"
#include "ctype.h"
#include "errno.h"
#include "fcntl.h"
#include "stdarg.h"
#include "stdbool.h"
#include "stddef.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

ssize_t read(int fd, void* buf, size_t count);
ssize_t write(int fd, const void* buf, size_t count);
int close(int fd);
off_t lseek(int fd, off_t offset, int whence);
int unlink(const char* pathname);
int rmdir(const char* pathname);
int chdir(const char* path);
char* getcwd(char* buf, size_t size);
void* sbrk(intptr_t increment);
int brk(void* addr);
unsigned int sleep(unsigned int seconds);
int usleep(unsigned int usec);
int isatty(int fd);
ssize_t readlink(const char* pathname, char* buf, size_t bufsiz);
int symlink(const char* target, const char* linkpath);
int link(const char* oldpath, const char* newpath);
pid_t getpid(void);
void _exit(int status) __attribute__((noreturn));
pid_t fork(void);
int execve(const char* pathname, char* const argv[], char* const envp[]);
int execv(const char* pathname, char* const argv[]);
int pipe(int pipefd[2]);
int dup(int oldfd);
int dup2(int oldfd, int newfd);
int ftruncate(int fd, off_t length);
uid_t getuid(void);
uid_t geteuid(void);
gid_t getgid(void);
gid_t getegid(void);
int setuid(uid_t uid);
int setgid(gid_t gid);

#endif
