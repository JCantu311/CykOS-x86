#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>

int vsscanf(const char* input, const char* format, va_list ap)
{
    if (!input || !format) return EOF;
    const char* in = input;
    int assigned = 0;
    va_list args;
    va_copy(args, ap);

    while (*format) {
        if (isspace((unsigned char)*format)) {
            while (isspace((unsigned char)*format)) ++format;
            while (isspace((unsigned char)*in)) ++in;
            continue;
        }
        if (*format != '%') {
            if (*in != *format) break;
            ++in;
            ++format;
            continue;
        }

        ++format;
        if (*format == '%') {
            if (*in != '%') break;
            ++in;
            ++format;
            continue;
        }

        int suppress = 0;
        if (*format == '*') {
            suppress = 1;
            ++format;
        }
        int width = 0;
        while (isdigit((unsigned char)*format)) {
            width = width * 10 + (*format - '0');
            ++format;
        }
        int long_value = 0;
        if (*format == 'l' || *format == 'h') {
            long_value = *format == 'l';
            ++format;
            if ((long_value && *format == 'l') || (!long_value && *format == 'h')) ++format;
        }

        while (isspace((unsigned char)*in)) ++in;
        if (*in == '\0') break;

        if (*format == 'd' || *format == 'i' || *format == 'u' ||
            *format == 'x' || *format == 'X' || *format == 'o') {
            int base = (*format == 'x' || *format == 'X') ? 16 :
                       *format == 'o' ? 8 : *format == 'i' ? 0 : 10;
            char* end = NULL;
            long value = strtol(in, &end, base);
            if (end == in) break;
            in = end;
            if (!suppress) {
                if (long_value) *va_arg(args, long*) = value;
                else *va_arg(args, int*) = (int)value;
                ++assigned;
            }
        } else if (*format == 'c') {
            int count = width ? width : 1;
            char* out = suppress ? NULL : va_arg(args, char*);
            for (int i = 0; i < count; ++i) {
                ++in;
                if (out) out[i] = in[-1];
            }
            if (!suppress) ++assigned;
        } else if (*format == 's') {
            char* out = suppress ? NULL : va_arg(args, char*);
            int copied = 0;
            while (*in && !isspace((unsigned char)*in)) {
                if (out) *out++ = *in;
                ++in;
                ++copied;
            }
            if (copied == 0) break;
            if (out) *out = '\0';
            if (!suppress) ++assigned;
        } else {
            break;
        }
        ++format;
    }

    va_end(args);
    return assigned;
}

int sscanf(const char* input, const char* format, ...)
{
    va_list ap;
    va_start(ap, format);
    int r = vsscanf(input, format, ap);
    va_end(ap);
    return r;
}

int vfscanf(FILE* stream, const char* format, va_list ap)
{
    if (!stream || !format) return EOF;
    char line[512];
    if (!fgets(line, sizeof(line), stream)) return EOF;
    return vsscanf(line, format, ap);
}

int fscanf(FILE* stream, const char* format, ...)
{
    va_list ap;
    va_start(ap, format);
    int r = vfscanf(stream, format, ap);
    va_end(ap);
    return r;
}

int scanf(const char* format, ...)
{
    va_list ap;
    va_start(ap, format);
    int r = vfscanf(stdin, format, ap);
    va_end(ap);
    return r;
}
