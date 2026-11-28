#include <unistd.h>
#include <errno.h>

int ftruncate(int fd, off_t length) {
    /* CykOS VFS has no truncate support yet. Stub returns success
     * for fd >= 0 as a best-effort placeholder. */
    if (fd < 0) {
        errno = EBADF;
        return -1;
    }
    (void)length;
    return 0;
}
