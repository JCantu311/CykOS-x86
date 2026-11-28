#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <strings.h>
#include <errno.h>

int strcasecmp(const char* left, const char* right) {
    while (*left && *right) {
        int a = tolower((unsigned char)*left++);
        int b = tolower((unsigned char)*right++);
        if (a != b) return a - b;
    }
    return (unsigned char)*left - (unsigned char)*right;
}

int strncasecmp(const char* left, const char* right, size_t count) {
    while (count && *left && *right) {
        int a = tolower((unsigned char)*left++);
        int b = tolower((unsigned char)*right++);
        if (a != b) return a - b;
        --count;
    }
    if (!count) return 0;
    return (unsigned char)*left - (unsigned char)*right;
}

size_t strlen(const char* s) {
    size_t len = 0;
    while (s && s[len]) len++;
    return len;
}

char* strcpy(char* dest, const char* src) {
    char* d = dest;
    while ((*d++ = *src++));
    return dest;
}

char* strncpy(char* dest, const char* src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++)
        dest[i] = src[i];
    for ( ; i < n; i++)
        dest[i] = '\0';
    return dest;
}

char* strcat(char* dest, const char* src) {
    char* d = dest;
    while (*d) d++;
    while ((*d++ = *src++));
    return dest;
}

char* strncat(char* dest, const char* src, size_t n) {
    char* d = dest;
    while (*d) d++;
    size_t i = 0;
    while (i < n && *src) {
        *d++ = *src++;
        i++;
    }
    *d = '\0';
    return dest;
}

int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return (unsigned char)*s1 - (unsigned char)*s2;
}

char* strerror(int error_number) {
    switch (error_number) {
        case 0: return (char*)"Success";
        case EPERM: return (char*)"Operation not permitted";
        case ENOENT: return (char*)"No such file or directory";
        case EINTR: return (char*)"Interrupted system call";
        case EIO: return (char*)"Input/output error";
        case EBADF: return (char*)"Bad file descriptor";
        case EACCES: return (char*)"Permission denied";
        case EEXIST: return (char*)"File exists";
        case ENOTDIR: return (char*)"Not a directory";
        case EISDIR: return (char*)"Is a directory";
        case EINVAL: return (char*)"Invalid argument";
        case ENOSPC: return (char*)"No space left on device";
        case EROFS: return (char*)"Read-only file system";
        case ENOSYS: return (char*)"Function not implemented";
        default: return (char*)"Unknown error";
    }
}

int strncmp(const char* s1, const char* s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i])
            return (unsigned char)s1[i] - (unsigned char)s2[i];
        if (s1[i] == '\0')
            return 0;
    }
    return 0;
}

char* strchr(const char* s, int c) {
    char ch = (char)c;
    while (*s) {
        if (*s == ch) return (char*)s;
        s++;
    }
    return (ch == '\0') ? (char*)s : NULL;
}

char* strrchr(const char* s, int c) {
    char ch = (char)c;
    const char* last = NULL;
    while (*s) {
        if (*s == ch) last = s;
        s++;
    }
    if (ch == '\0') return (char*)s;
    return (char*)last;
}

char* strstr(const char* haystack, const char* needle) {
    if (!*needle) return (char*)haystack;
    for (; *haystack; haystack++) {
        if (*haystack == *needle) {
            const char* h = haystack;
            const char* n = needle;
            while (*h && *n && (*h == *n)) {
                h++;
                n++;
            }
            if (!*n) return (char*)haystack;
        }
    }
    return NULL;
}

char* strpbrk(const char* s, const char* accept) {
    for (; *s; s++) {
        if (strchr(accept, *s)) return (char*)s;
    }
    return NULL;
}

size_t strspn(const char* s, const char* accept) {
    size_t length = 0;
    while (s[length] && strchr(accept, s[length])) length++;
    return length;
}

char* strdup(const char* s) {
    size_t len = strlen(s);
    char* copy = (char*)malloc(len + 1);
    if (!copy) return NULL;
    memcpy(copy, s, len + 1);
    return copy;
}

char* strndup(const char* s, size_t n) {
    size_t len = strlen(s);
    if (len > n) len = n;
    char* copy = (char*)malloc(len + 1);
    if (!copy) return NULL;
    memcpy(copy, s, len);
    copy[len] = '\0';
    return copy;
}

size_t strcspn(const char* s, const char* reject) {
    size_t count = 0;
    for (; *s; s++) {
        if (strchr(reject, *s)) break;
        count++;
    }
    return count;
}

char* strtok_r(char* str, const char* delim, char** saveptr) {
    if (str != NULL) *saveptr = str;
    if (*saveptr == NULL) return NULL;

    char* pos = *saveptr;
    while (*pos && strchr(delim, *pos)) pos++;
    if (*pos == '\0') { *saveptr = NULL; return NULL; }

    char* start = pos;
    while (*pos && !strchr(delim, *pos)) pos++;
    if (*pos) { *pos = '\0'; *saveptr = pos + 1; }
    else { *saveptr = NULL; }
    return start;
}

static char* strtok_pos = NULL;

char* strtok(char* str, const char* delim) {
    if (str != NULL) {
        strtok_pos = str;
    }
    if (strtok_pos == NULL) return NULL;

    while (*strtok_pos && strchr(delim, *strtok_pos)) {
        strtok_pos++;
    }
    if (*strtok_pos == '\0') {
        strtok_pos = NULL;
        return NULL;
    }

    char* start = strtok_pos;
    while (*strtok_pos && !strchr(delim, *strtok_pos)) {
        strtok_pos++;
    }

    if (*strtok_pos) {
        *strtok_pos = '\0';
        strtok_pos++;
    } else {
        strtok_pos = NULL;
    }

    return start;
}

void* memset(void* s, int c, size_t n) {
    unsigned char* p = (unsigned char*)s;
    unsigned char val = (unsigned char)c;
    for (size_t i = 0; i < n; i++) p[i] = val;
    return s;
}

void* memcpy(void* dest, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dest;
}

void* memmove(void* dest, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    if (d < s) {
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    } else if (d > s) {
        for (size_t i = n; i > 0; i--) d[i - 1] = s[i - 1];
    }
    return dest;
}

int memcmp(const void* s1, const void* s2, size_t n) {
    const unsigned char* p1 = (const unsigned char*)s1;
    const unsigned char* p2 = (const unsigned char*)s2;
    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) return (int)p1[i] - (int)p2[i];
    }
    return 0;
}

void* memchr(const void* s, int c, size_t n) {
    const unsigned char* p = (const unsigned char*)s;
    unsigned char ch = (unsigned char)c;
    for (size_t i = 0; i < n; i++) {
        if (p[i] == ch) return (void*)(p + i);
    }
    return NULL;
}
