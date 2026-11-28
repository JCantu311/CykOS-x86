#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#define MAX_ENV_VARS 64

static char* g_env_storage[MAX_ENV_VARS + 1];
static int g_env_init = 0;
char** environ = g_env_storage;

static void env_ensure_init(void) {
    if (!g_env_init) {
        g_env_init = 1;
        g_env_storage[0] = "PATH=/USR/BIN:/BIN";
        g_env_storage[1] = "HOME=/HOME/USER";
        g_env_storage[2] = "TMPDIR=/TMP";
        g_env_storage[3] = (void*)0;
    }
}

void __cykos_init_env(char** envp) {
    if (!envp || !envp[0]) {
        env_ensure_init();
        return;
    }
    g_env_init = 1;
    int i = 0;
    while (i < MAX_ENV_VARS && envp[i]) {
        g_env_storage[i] = envp[i];
        i++;
    }
    g_env_storage[i] = (void*)0;
}

char* getenv(const char* name) {
    if (!name || !name[0]) return (void*)0;
    env_ensure_init();
    size_t len = strlen(name);
    for (int i = 0; i < MAX_ENV_VARS && g_env_storage[i]; i++) {
        if (strncmp(g_env_storage[i], name, len) == 0 && g_env_storage[i][len] == '=') {
            return g_env_storage[i] + len + 1;
        }
    }
    return (void*)0;
}

int setenv(const char* name, const char* value, int overwrite) {
    if (!name || !name[0] || strchr(name, '=')) {
        errno = EINVAL;
        return -1;
    }
    env_ensure_init();
    size_t nlen = strlen(name);
    size_t vlen = value ? strlen(value) : 0;

    int found = -1;
    int count = 0;
    while (count < MAX_ENV_VARS && g_env_storage[count]) {
        if (strncmp(g_env_storage[count], name, nlen) == 0 && g_env_storage[count][nlen] == '=') {
            found = count;
        }
        count++;
    }
    if (found >= 0 && !overwrite) return 0;
    if (found < 0 && count >= MAX_ENV_VARS) {
        errno = ENOMEM;
        return -1;
    }

    char* entry = (char*)malloc(nlen + 1 + vlen + 1);
    if (!entry) {
        errno = ENOMEM;
        return -1;
    }
    memcpy(entry, name, nlen);
    entry[nlen] = '=';
    if (value && vlen) memcpy(entry + nlen + 1, value, vlen);
    entry[nlen + 1 + vlen] = '\0';

    if (found >= 0) {
        g_env_storage[found] = entry;
    } else {
        g_env_storage[count] = entry;
        g_env_storage[count + 1] = (void*)0;
    }
    return 0;
}

int unsetenv(const char* name) {
    if (!name || !name[0] || strchr(name, '=')) {
        errno = EINVAL;
        return -1;
    }
    env_ensure_init();
    size_t len = strlen(name);
    for (int i = 0; i < MAX_ENV_VARS && g_env_storage[i]; i++) {
        if (strncmp(g_env_storage[i], name, len) == 0 && g_env_storage[i][len] == '=') {
            for (int j = i; g_env_storage[j]; j++) {
                g_env_storage[j] = g_env_storage[j + 1];
            }
            return 0;
        }
    }
    return 0;
}

int putenv(char* string) {
    if (!string) return -1;
    char* eq = strchr(string, '=');
    if (!eq) return unsetenv(string);
    size_t nlen = (size_t)(eq - string);
    char name[128];
    if (nlen >= sizeof(name)) return -1;
    memcpy(name, string, nlen);
    name[nlen] = '\0';
    return setenv(name, eq + 1, 1);
}

static unsigned int g_rand_state = 1;

void srand(unsigned int seed) {
    g_rand_state = seed ? seed : 1u;
}

int rand(void) {
    g_rand_state = g_rand_state * 1103515245u + 12345u;
    return (int)((g_rand_state >> 16) & 0x7FFFu);
}

long atol(const char* nptr) {
    return strtol(nptr, (void*)0, 10);
}

long long atoll(const char* nptr) {
    return strtoll(nptr, (void*)0, 10);
}

long labs(long j) {
    return j < 0 ? -j : j;
}

unsigned long long strtoull(const char* nptr, char** endptr, int base) {
    const char* s = nptr;
    while (isspace((unsigned char)*s)) s++;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;

    if (base == 0) {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
        else if (s[0] == '0') { base = 8; s++; }
        else base = 10;
    } else if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }

    unsigned long long acc = 0;
    int any = 0;
    for (; *s; s++) {
        int d;
        unsigned char c = (unsigned char)*s;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'z') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z') d = c - 'A' + 10;
        else break;
        if (d >= base) break;
        acc = acc * (unsigned long long)base + (unsigned long long)d;
        any = 1;
    }
    if (endptr) *endptr = (char*)(any ? s : nptr);
    return neg ? (unsigned long long)(-(long long)acc) : acc;
}

long long strtoll(const char* nptr, char** endptr, int base) {
    const char* s = nptr;
    while (isspace((unsigned char)*s)) s++;
    int neg = (*s == '-');
    unsigned long long v = strtoull(nptr, endptr, base);
    return neg ? -(long long)v : (long long)v;
}

unsigned long strtoul(const char* nptr, char** endptr, int base) {
    return (unsigned long)strtoull(nptr, endptr, base);
}

static unsigned int g_mkstemp_seq = 0;

int mkstemp(char* tmpl) {
    if (!tmpl) { errno = EINVAL; return -1; }
    size_t len = strlen(tmpl);
    if (len < 6 || strcmp(tmpl + len - 6, "XXXXXX") != 0) {
        errno = EINVAL;
        return -1;
    }
    for (int attempt = 0; attempt < 100; attempt++) {
        unsigned int n = g_mkstemp_seq++;
        for (int i = 5; i >= 0; i--) {
            tmpl[len - 6 + (size_t)i] = (char)('0' + (n % 10u));
            n /= 10u;
        }
        int fd = open(tmpl, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0) return fd;
    }
    errno = EEXIST;
    return -1;
}

char* realpath(const char* path, char* resolved_path) {
    if (!path || !path[0]) { errno = EINVAL; return (void*)0; }
    char* out = resolved_path ? resolved_path : (char*)malloc(256);
    if (!out) { errno = ENOMEM; return (void*)0; }

    if (path[0] == '/') {
        strncpy(out, path, 255);
        out[255] = '\0';
    } else {
        char cwd[128];
        if (!getcwd(cwd, sizeof(cwd))) {
            if (!resolved_path) free(out);
            return (void*)0;
        }
        size_t clen = strlen(cwd);
        strncpy(out, cwd, 255);
        out[255] = '\0';
        if (clen > 1 && clen < 254) {
            out[clen++] = '/';
            out[clen] = '\0';
        }
        strncat(out, path, 255 - clen);
    }
    return out;
}
