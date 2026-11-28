int cykmath_add(int a, int b) {
    return a + b;
}

int cykmath_mul(int a, int b) {
    return a * b;
}

int cykmath_fib(int n) {
    if (n <= 1) return n;
    int a = 0;
    int b = 1;
    for (int i = 2; i <= n; i++) {
        int next = a + b;
        a = b;
        b = next;
    }
    return b;
}
