#include <sys/utsname.h>
#include <string.h>

int uname(struct utsname* buf) {
    if (!buf) return -1;
    memset(buf, 0, sizeof(*buf));
    strcpy(buf->sysname, "CykOS");
    strcpy(buf->nodename, "cykos");
    strcpy(buf->release, "0.1.0");
    strcpy(buf->version, "CykOS 0.1.0");
    strcpy(buf->machine, "i686");
    return 0;
}
