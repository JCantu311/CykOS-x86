#ifndef _SYS_WAIT_H
#define _SYS_WAIT_H

#include <sys/types.h>

#define WNOHANG 1

#define WIFEXITED(status)   (((status) & 0x7F) == 0)
#define WEXITSTATUS(status) (((status) >> 8) & 0xFF)
#define WIFSIGNALED(status) (((status) & 0x7F) != 0)
#define WTERMSIG(status)    ((status) & 0x7F)

pid_t wait(int* status);
pid_t waitpid(pid_t pid, int* status, int options);

#endif /* _SYS_WAIT_H */
