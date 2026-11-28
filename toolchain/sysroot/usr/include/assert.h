#ifndef _ASSERT_H
#define _ASSERT_H

#ifdef NDEBUG
#define assert(expression) ((void)0)
#else
#include <stdio.h>
#include <stdlib.h>

static inline void __cykos_assert_fail(const char* expression,
                                      const char* file,
                                      int line,
                                      const char* function)
{
    fprintf(stderr, "Assertion failed: %s, function %s, file %s, line %d\n",
            expression, function, file, line);
    abort();
}

#define assert(expression) \
    ((expression) ? (void)0 : \
     __cykos_assert_fail(#expression, __FILE__, __LINE__, __func__))
#endif

#endif
