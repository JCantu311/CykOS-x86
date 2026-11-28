#include <sys/mman.h>
#include <errno.h>
#include "syscall.h"

#define SYS_MMAP   211
#define SYS_MUNMAP 212

void* mmap(void* addr, size_t length, int prot, int flags, int fd, long offset) {
    (void)fd;
    (void)offset;
    if (length == 0) {
        errno = EINVAL;
        return MAP_FAILED;
    }
    unsigned int res = (unsigned int)__syscall4(SYS_MMAP, (int)addr, (int)length, prot, flags);
    if (res == (unsigned int)-1 || res == 0) {
        errno = ENOMEM;
        return MAP_FAILED;
    }
    return (void*)res;
}

int munmap(void* addr, size_t length) {
    int res = __syscall2(SYS_MUNMAP, (int)addr, (int)length);
    if (res < 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}
