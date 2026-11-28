#include <stdio.h>
#include <dlfcn.h>

/* Resolved at load time via DT_NEEDED LIBCYKMATH.SO */
extern int cykmath_add(int a, int b);
extern int cykmath_mul(int a, int b);

typedef int (*fib_fn_t)(int);

int main(void) {
    printf("=== CykOS Dynamic Shared Library Test ===\n");

    int sum = cykmath_add(19, 23);
    int prod = cykmath_mul(6, 7);
    printf("[DT_NEEDED] cykmath_add(19, 23) = %d\n", sum);
    printf("[DT_NEEDED] cykmath_mul(6, 7)   = %d\n", prod);

    void* handle = dlopen("/USR/LIB/LIBCYKMATH.SO", RTLD_NOW);
    if (!handle) {
        printf("[dlopen] FAILED: %s\n", dlerror());
        return 1;
    }
    printf("[dlopen] Loaded LIBCYKMATH.SO (handle=%p)\n", handle);

    fib_fn_t fib = (fib_fn_t)dlsym(handle, "cykmath_fib");
    if (!fib) {
        printf("[dlsym] FAILED: %s\n", dlerror());
        dlclose(handle);
        return 1;
    }
    printf("[dlsym] cykmath_fib(10) = %d\n", fib(10));

    dlclose(handle);
    printf("Dynamic shared library test PASSED!\n");
    return 0;
}
