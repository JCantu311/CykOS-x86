#include <float.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

double floor(double value) {
    if (value > (double)LONG_MAX || value < (double)LONG_MIN || value != value)
        return value;
    long whole = (long)value;
    if ((double)whole > value) whole--;
    return (double)whole;
}

double frexp(double value, int* exponent) {
    if (!exponent) return value;
    *exponent = 0;
    if (value == 0.0 || value != value || value > DBL_MAX || value < -DBL_MAX)
        return value;
    double magnitude = value < 0.0 ? -value : value;
    while (magnitude >= 1.0) {
        magnitude *= 0.5;
        value *= 0.5;
        (*exponent)++;
    }
    while (magnitude < 0.5) {
        magnitude *= 2.0;
        value *= 2.0;
        (*exponent)--;
    }
    return value;
}

double ldexp(double value, int exponent) {
    if (value == 0.0 || value != value) return value;
    if (exponent > 2048) return value < 0.0 ? -HUGE_VAL : HUGE_VAL;
    if (exponent < -2048) return value < 0.0 ? -0.0 : 0.0;
    while (exponent > 0) { value *= 2.0; exponent--; }
    while (exponent < 0) { value *= 0.5; exponent++; }
    return value;
}

double strtod(const char* text, char** endptr) {
    const char* p = text;
    while (*p == ' ' || (*p >= '\t' && *p <= '\r')) p++;
    int negative = 0;
    if (*p == '+' || *p == '-') { negative = (*p == '-'); p++; }

    double value = 0.0;
    int digits = 0;
    while (*p >= '0' && *p <= '9') {
        value = value * 10.0 + (*p++ - '0');
        digits++;
    }
    if (*p == '.') {
        p++;
        double place = 0.1;
        while (*p >= '0' && *p <= '9') {
            value += (*p++ - '0') * place;
            place *= 0.1;
            digits++;
        }
    }
    if (!digits) {
        if (endptr) *endptr = (char*)text;
        return 0.0;
    }

    int exponent_negative = 0;
    int exponent = 0;
    const char* exponent_mark = p;
    if (*p == 'e' || *p == 'E') {
        p++;
        if (*p == '+' || *p == '-') { exponent_negative = (*p == '-'); p++; }
        if (*p < '0' || *p > '9') {
            p = exponent_mark;
        } else {
            while (*p >= '0' && *p <= '9') {
                if (exponent < 400) exponent = exponent * 10 + (*p - '0');
                p++;
            }
        }
    }
    if (exponent > 400) exponent = 400;
    while (exponent-- > 0) value = exponent_negative ? value * 0.1 : value * 10.0;
    if (negative) value = -value;
    if (endptr) *endptr = (char*)p;
    return value;
}

int strcoll(const char* left, const char* right) {
    return strcmp(left, right);
}

struct lconv* localeconv(void) {
    static struct lconv locale = { .decimal_point = "." };
    return &locale;
}

char* setlocale(int category, const char* locale) {
    (void)category;
    (void)locale;
    return (char*)"C";
}

unsigned int cykos_lua_makeseed(void) {
    static unsigned int state = 0x9E3779B9u;
    state ^= (unsigned int)&state;
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}
