#include <wchar.h>
#include <errno.h>

size_t wcslen(const wchar_t* s) {
    size_t len = 0;
    while (s && s[len]) len++;
    return len;
}

int wcscmp(const wchar_t* s1, const wchar_t* s2) {
    while (*s1 && *s1 == *s2) { s1++; s2++; }
    return (int)*s1 - (int)*s2;
}

wchar_t* wcschr(const wchar_t* s, wchar_t c) {
    while (*s) {
        if (*s == c) return (wchar_t*)s;
        s++;
    }
    return (c == 0) ? (wchar_t*)s : NULL;
}

wchar_t* wmemcpy(wchar_t* dest, const wchar_t* src, size_t n) {
    for (size_t i = 0; i < n; i++) dest[i] = src[i];
    return dest;
}

wchar_t* wmemset(wchar_t* s, wchar_t c, size_t n) {
    for (size_t i = 0; i < n; i++) s[i] = c;
    return s;
}

int mbsinit(const mbstate_t* ps) {
    return (!ps || ps->count == 0) ? 1 : 0;
}

size_t mbrtowc(wchar_t* pwc, const char* s, size_t n, mbstate_t* ps) {
    (void)ps;
    if (!s) return 0;
    if (n == 0) return (size_t)-2;
    unsigned char ch = (unsigned char)s[0];
    if (pwc) *pwc = (wchar_t)ch;
    return ch ? 1 : 0;
}

size_t wcrtomb(char* s, wchar_t wc, mbstate_t* ps) {
    (void)ps;
    if (!s) return 1;
    if (wc > 0xFF) {
        errno = EILSEQ;
        return (size_t)-1;
    }
    *s = (char)(unsigned char)wc;
    return 1;
}

int wcwidth(wchar_t c) {
    if (c == 0) return 0;
    if (c < 32 || c == 127) return -1;
    return 1;
}

size_t mbstowcs(wchar_t* pwcs, const char* s, size_t n) {
    if (!s) return 0;
    size_t count = 0;
    if (!pwcs) {
        while (s[count]) count++;
        return count;
    }
    while (count < n && s[count]) {
        pwcs[count] = (wchar_t)(unsigned char)s[count];
        count++;
    }
    if (count < n) pwcs[count] = 0;
    return count;
}

size_t wcstombs(char* s, const wchar_t* pwcs, size_t n) {
    if (!pwcs) return 0;
    size_t count = 0;
    if (!s) {
        while (pwcs[count]) count++;
        return count;
    }
    while (count < n && pwcs[count]) {
        s[count] = (char)(unsigned char)pwcs[count];
        count++;
    }
    if (count < n) s[count] = '\0';
    return count;
}
