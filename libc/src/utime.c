#include <utime.h>
#include <errno.h>

int utime(const char* filename, const struct utimbuf* times) {
    (void)times;
    if (!filename || !filename[0]) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}
