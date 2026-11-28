#ifndef _STDINT_H
#define _STDINT_H

typedef signed char        int8_t;
typedef short              int16_t;
typedef int                int32_t;
typedef long long          int64_t;

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;

typedef int32_t            intptr_t;
typedef uint32_t           uintptr_t;
typedef int64_t            intmax_t;
typedef uint64_t           uintmax_t;

#define INT32_MAX 2147483647
#define INT32_MIN (-2147483647 - 1)
#define UINT32_MAX 0xFFFFFFFFU
#define INT64_MAX  9223372036854775807LL
#define INT64_MIN  (-9223372036854775807LL - 1LL)
#define UINT64_MAX 18446744073709551615ULL
#define INTMAX_MAX INT64_MAX
#define INTMAX_MIN INT64_MIN
#define UINTMAX_MAX UINT64_MAX
#define INTPTR_MAX INT32_MAX
#define INTPTR_MIN INT32_MIN
#define UINTPTR_MAX UINT32_MAX

#endif
