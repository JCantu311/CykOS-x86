#ifndef _UNISTD_H
#define _UNISTD_H

#include <sys/types.h>

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
int isatty(int fd);

#endif
