#include <dlfcn.h>
#include "syscall.h"

#define SYS_DLOPEN  208
#define SYS_DLSYM   209
#define SYS_DLCLOSE 210

static const char* g_dl_last_error = (void*)0;

void* dlopen(const char* filename, int flags) {
    if (!filename) {
        g_dl_last_error = "dlopen: NULL filename";
        return (void*)0;
    }
    unsigned int h = (unsigned int)__syscall2(SYS_DLOPEN, (int)filename, flags);
    if (!h) {
        g_dl_last_error = "dlopen: failed to load shared library";
        return (void*)0;
    }
    g_dl_last_error = (void*)0;
    return (void*)h;
}

void* dlsym(void* handle, const char* symbol) {
    if (!symbol) {
        g_dl_last_error = "dlsym: NULL symbol";
        return (void*)0;
    }
    unsigned int addr = (unsigned int)__syscall2(SYS_DLSYM, (int)handle, (int)symbol);
    if (!addr) {
        g_dl_last_error = "dlsym: symbol not found";
        return (void*)0;
    }
    g_dl_last_error = (void*)0;
    return (void*)addr;
}

int dlclose(void* handle) {
    int res = __syscall1(SYS_DLCLOSE, (int)handle);
    if (res != 0) {
        g_dl_last_error = "dlclose: invalid handle";
    } else {
        g_dl_last_error = (void*)0;
    }
    return res;
}

char* dlerror(void) {
    char* err = (char*)g_dl_last_error;
    g_dl_last_error = (void*)0;
    return err;
}
