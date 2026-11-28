#include <signal.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

/* Minimal signal() / sigaction() stubs for CykOS.
 * CykOS currently has no kernel-level signal delivery.
 * These are needed to compile programs like GCC/TCC that
 * register cleanup handlers with signal(SIGINT, ...) etc.
 */

static sighandler_t g_handlers[32];
static int g_handlers_init = 0;

static void signal_ensure_init(void) {
    if (!g_handlers_init) {
        g_handlers_init = 1;
        for (int i = 0; i < 32; i++)
            g_handlers[i] = SIG_DFL;
    }
}

sighandler_t signal(int signum, sighandler_t handler) {
    signal_ensure_init();
    if (signum < 1 || signum >= 32 || signum == SIGKILL) {
        errno = EINVAL;
        return SIG_ERR;
    }
    sighandler_t old = g_handlers[signum];
    g_handlers[signum] = handler;
    return old;
}

int sigaction(int signum, const struct sigaction* act,
              struct sigaction* oldact)
{
    signal_ensure_init();
    if (signum < 1 || signum >= 32 || signum == SIGKILL) {
        errno = EINVAL;
        return -1;
    }
    if (oldact) {
        memset(oldact, 0, sizeof(*oldact));
        oldact->sa_handler = g_handlers[signum];
    }
    if (act) {
        g_handlers[signum] = act->sa_handler;
    }
    return 0;
}

int raise(int sig) {
    signal_ensure_init();
    if (sig < 1 || sig >= 32) {
        errno = EINVAL;
        return -1;
    }
    if (sig == SIGABRT) {
        abort();
    }
    sighandler_t h = g_handlers[sig];
    if (h == SIG_DFL || h == SIG_IGN) return 0;
    h(sig);
    return 0;
}

int sigemptyset(sigset_t* set) {
    if (!set) return -1;
    *set = 0;
    return 0;
}

int sigfillset(sigset_t* set) {
    if (!set) return -1;
    *set = ~(sigset_t)0;
    return 0;
}

int sigaddset(sigset_t* set, int signum) {
    if (!set || signum < 1 || signum >= 32) {
        errno = EINVAL;
        return -1;
    }
    *set |= (1u << signum);
    return 0;
}

int sigdelset(sigset_t* set, int signum) {
    if (!set || signum < 1 || signum >= 32) {
        errno = EINVAL;
        return -1;
    }
    *set &= ~(1u << signum);
    return 0;
}

int sigismember(const sigset_t* set, int signum) {
    if (!set || signum < 1 || signum >= 32) {
        errno = EINVAL;
        return -1;
    }
    return (*set & (1u << signum)) ? 1 : 0;
}

int sigprocmask(int how, const sigset_t* set, sigset_t* oldset) {
    (void)how; (void)set;
    if (oldset) *oldset = 0;
    return 0;
}

int kill(pid_t pid, int sig) {
    (void)pid;
    if (sig == 0) return 0;
    return raise(sig);
}
