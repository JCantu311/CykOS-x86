#include <signal.h>
#include <stdlib.h>

int raise(int sig) {
    if (sig == SIGABRT) {
        abort();
    }
    exit(128 + sig);
}
