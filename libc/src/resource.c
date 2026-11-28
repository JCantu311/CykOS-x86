#include <sys/resource.h>
#include <errno.h>
#include <string.h>

int getrlimit(int resource, struct rlimit* rlim) {
    if (!rlim || resource < 0 || resource >= RLIM_NLIMITS) {
        errno = EINVAL;
        return -1;
    }
    switch (resource) {
        case RLIMIT_NOFILE:
            rlim->rlim_cur = 32;
            rlim->rlim_max = 32;
            break;
        case RLIMIT_STACK:
            rlim->rlim_cur = 256u * 1024u;
            rlim->rlim_max = 1024u * 1024u;
            break;
        default:
            rlim->rlim_cur = RLIM_INFINITY;
            rlim->rlim_max = RLIM_INFINITY;
            break;
    }
    return 0;
}

int setrlimit(int resource, const struct rlimit* rlim) {
    if (!rlim || resource < 0 || resource >= RLIM_NLIMITS) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int getrusage(int who, struct rusage* usage) {
    if (!usage) {
        errno = EINVAL;
        return -1;
    }
    (void)who;
    memset(usage, 0, sizeof(*usage));
    return 0;
}
