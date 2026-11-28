#ifndef _SYS_SELECT_H
#define _SYS_SELECT_H

#include <sys/types.h>
#include <sys/time.h>

#define FD_SETSIZE 32

typedef struct {
    unsigned int fds_bits[(FD_SETSIZE + 31) / 32];
} fd_set;

#define FD_ZERO(set)   do { (set)->fds_bits[0] = 0; } while(0)
#define FD_SET(fd, set)   ((set)->fds_bits[0] |= (1u << (fd)))
#define FD_CLR(fd, set)   ((set)->fds_bits[0] &= ~(1u << (fd)))
#define FD_ISSET(fd, set) ((set)->fds_bits[0] & (1u << (fd)))

int select(int nfds, fd_set* readfds, fd_set* writefds,
           fd_set* exceptfds, struct timeval* timeout);

#endif
