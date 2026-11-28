#ifndef _WCHAR_H
#define _WCHAR_H

#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>

typedef int wint_t;
typedef struct {
    unsigned int count;
    unsigned int value;
} mbstate_t;

#ifndef WEOF
#define WEOF ((wint_t)-1)
#endif

size_t wcslen(const wchar_t* s);
int    wcscmp(const wchar_t* s1, const wchar_t* s2);
wchar_t* wcschr(const wchar_t* s, wchar_t c);
wchar_t* wmemcpy(wchar_t* dest, const wchar_t* src, size_t n);
wchar_t* wmemset(wchar_t* s, wchar_t c, size_t n);

int    mbsinit(const mbstate_t* ps);
size_t mbrtowc(wchar_t* pwc, const char* s, size_t n, mbstate_t* ps);
size_t wcrtomb(char* s, wchar_t wc, mbstate_t* ps);
int    wcwidth(wchar_t c);

#endif
