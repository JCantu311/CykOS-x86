#ifndef _STDDEF_H
#define _STDDEF_H

#ifndef NULL
#define NULL ((void*)0)
#endif

typedef unsigned int size_t;
typedef int          ptrdiff_t;
typedef unsigned short wchar_t;

#define offsetof(type, member) ((size_t)&(((type*)0)->member))

#endif
