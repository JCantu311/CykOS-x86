#include <fcntl.h>
#include <stdarg.h>
#include <errno.h>
#include "syscall.h"

#define SYS_FCNTL 55

int fcntl(int fd, int cmd, ...) {
    int arg = 0;
    if (cmd == F_DUPFD || cmd == F_SETFD || cmd == F_SETFL) {
        va_list ap;
        va_start(ap, cmd);
        arg = va_arg(ap, int);
        va_end(ap);
    }
    int ret = __syscall3(SYS_FCNTL, fd, cmd, arg);
    if (ret < 0) {
        errno = EBADF;
        return -1;
    }
    return ret;
}
