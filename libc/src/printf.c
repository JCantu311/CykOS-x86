#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>

typedef struct {
    char* buf;
    size_t size;
    size_t pos;
    FILE* stream;
} out_target_t;

static void emit_char(out_target_t* target, char c) {
    if (target->stream) {
        fputc(c, target->stream);
        target->pos++;
    } else if (target->buf) {
        if (target->pos + 1 < target->size) {
            target->buf[target->pos] = c;
        }
        target->pos++;
    } else {
        target->pos++;
    }
}

static void emit_string(out_target_t* target, const char* s, int width, int pad_zero, int left_align) {
    if (!s) s = "(null)";
    int len = (int)strlen(s);
    int padding = (width > len) ? (width - len) : 0;

    if (!left_align && padding > 0) {
        char pad = pad_zero ? '0' : ' ';
        while (padding--) emit_char(target, pad);
    }
    while (*s) {
        emit_char(target, *s++);
    }
    if (left_align && padding > 0) {
        while (padding--) emit_char(target, ' ');
    }
}

static void emit_number(out_target_t* target, uint32_t val, int is_signed, int base, int uppercase, int width, int pad_zero, int left_align) {
    char buf[32];
    int idx = 0;
    int negative = 0;

    if (is_signed && (int32_t)val < 0) {
        negative = 1;
        val = (uint32_t)(-(int32_t)val);
    }

    const char* digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";

    if (val == 0) {
        buf[idx++] = '0';
    } else {
        while (val > 0) {
            buf[idx++] = digits[val % base];
            val /= base;
        }
    }

    int len = idx + (negative ? 1 : 0);
    int padding = (width > len) ? (width - len) : 0;

    if (!left_align && !pad_zero && padding > 0) {
        while (padding--) emit_char(target, ' ');
    }
    if (negative) {
        emit_char(target, '-');
    }
    if (!left_align && pad_zero && padding > 0) {
        while (padding--) emit_char(target, '0');
    }

    while (idx > 0) {
        emit_char(target, buf[--idx]);
    }

    if (left_align && padding > 0) {
        while (padding--) emit_char(target, ' ');
    }
}

int vsnprintf(char* str, size_t size, const char* format, va_list ap) {
    out_target_t target = { .buf = str, .size = size, .pos = 0, .stream = NULL };
    const char* p = format;

    while (*p) {
        if (*p != '%') {
            emit_char(&target, *p++);
            continue;
        }
        p++; // skip '%'

        // Flags
        int left_align = 0;
        int pad_zero = 0;
        while (*p == '-' || *p == '0') {
            if (*p == '-') left_align = 1;
            if (*p == '0') pad_zero = 1;
            p++;
        }
        if (left_align) pad_zero = 0;

        // Width
        int width = 0;
        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (*p - '0');
            p++;
        }

        // Length modifiers (ignored for 32-bit: l, h)
        while (*p == 'l' || *p == 'h') p++;

        switch (*p) {
            case 'c': {
                char c = (char)va_arg(ap, int);
                emit_char(&target, c);
                break;
            }
            case 's': {
                const char* s = va_arg(ap, const char*);
                emit_string(&target, s, width, pad_zero, left_align);
                break;
            }
            case 'd':
            case 'i': {
                int val = va_arg(ap, int);
                emit_number(&target, (uint32_t)val, 1, 10, 0, width, pad_zero, left_align);
                break;
            }
            case 'u': {
                unsigned int val = va_arg(ap, unsigned int);
                emit_number(&target, val, 0, 10, 0, width, pad_zero, left_align);
                break;
            }
            case 'x': {
                unsigned int val = va_arg(ap, unsigned int);
                emit_number(&target, val, 0, 16, 0, width, pad_zero, left_align);
                break;
            }
            case 'X': {
                unsigned int val = va_arg(ap, unsigned int);
                emit_number(&target, val, 0, 16, 1, width, pad_zero, left_align);
                break;
            }
            case 'p': {
                uint32_t val = (uint32_t)va_arg(ap, void*);
                emit_string(&target, "0x", 0, 0, 0);
                emit_number(&target, val, 0, 16, 0, width, 1, left_align);
                break;
            }
            case '%': {
                emit_char(&target, '%');
                break;
            }
            default: {
                emit_char(&target, '%');
                if (*p) emit_char(&target, *p);
                break;
            }
        }
        if (*p) p++;
    }

    if (str && size > 0) {
        if (target.pos < size) {
            str[target.pos] = '\0';
        } else {
            str[size - 1] = '\0';
        }
    }
    return (int)target.pos;
}

int snprintf(char* str, size_t size, const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vsnprintf(str, size, format, ap);
    va_end(ap);
    return ret;
}

int sprintf(char* str, const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vsnprintf(str, 0x7FFFFFFF, format, ap);
    va_end(ap);
    return ret;
}

int vsprintf(char* str, const char* format, va_list ap) {
    return vsnprintf(str, 0x7FFFFFFF, format, ap);
}

int vfprintf(FILE* stream, const char* format, va_list ap) {
    out_target_t target = { .buf = NULL, .size = 0, .pos = 0, .stream = stream };
    char temp[1024];
    va_list ap_copy;
    va_copy(ap_copy, ap);
    int len = vsnprintf(temp, sizeof(temp), format, ap_copy);
    va_end(ap_copy);
    if (len > 0) {
        fwrite(temp, 1, (size_t)len, stream);
    }
    return len;
}

int fprintf(FILE* stream, const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vfprintf(stream, format, ap);
    va_end(ap);
    return ret;
}

int vprintf(const char* format, va_list ap) {
    return vfprintf(stdout, format, ap);
}

int printf(const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vfprintf(stdout, format, ap);
    va_end(ap);
    return ret;
}
