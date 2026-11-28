#include <sys/select.h>
#include <errno.h>

/* Stub: CykOS has no kernel select/poll. Returns immediately
 * pretending all fds are ready. Enough for many programs. */
int select(int nfds, fd_set* readfds, fd_set* writefds,
           fd_set* exceptfds, struct timeval* timeout)
{
    (void)timeout;
    int count = 0;
    for (int fd = 0; fd < nfds && fd < FD_SETSIZE; fd++) {
        if (readfds && FD_ISSET(fd, readfds)) count++;
        if (writefds && FD_ISSET(fd, writefds)) count++;
    }
    if (exceptfds) FD_ZERO(exceptfds);
    if (count == 0) {
        errno = ENOSYS;
        return -1;
    }
    return count;
}
