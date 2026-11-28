#include <time.h>
#include <sys/time.h>
#include "syscall.h"

#define SYS_GET_TICKS_MS 201
/* Base epoch: 2026-01-01 00:00:00 UTC */
#define CYKOS_EPOCH_BASE 1767225600L

static struct tm g_tm_buf;

clock_t clock(void) {
    return (clock_t)__syscall0(SYS_GET_TICKS_MS);
}

time_t time(time_t* tloc) {
    unsigned int ms = (unsigned int)__syscall0(SYS_GET_TICKS_MS);
    time_t now = CYKOS_EPOCH_BASE + (time_t)(ms / 1000u);
    if (tloc) *tloc = now;
    return now;
}

int gettimeofday(struct timeval* tv, struct timezone* tz) {
    if (tv) {
        unsigned int ms = (unsigned int)__syscall0(SYS_GET_TICKS_MS);
        tv->tv_sec = CYKOS_EPOCH_BASE + (time_t)(ms / 1000u);
        tv->tv_usec = (long)((ms % 1000u) * 1000u);
    }
    if (tz) {
        tz->tz_minuteswest = 0;
        tz->tz_dsttime = 0;
    }
    return 0;
}

double difftime(time_t time1, time_t time0) {
    return (double)(time1 - time0);
}

static int is_leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

struct tm* gmtime(const time_t* timep) {
    static const int mdays[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    time_t t = timep ? *timep : 0;
    long days = t / 86400L;
    long rem = t % 86400L;
    if (rem < 0) { rem += 86400L; days--; }

    g_tm_buf.tm_hour = (int)(rem / 3600L);
    rem %= 3600L;
    g_tm_buf.tm_min = (int)(rem / 60L);
    g_tm_buf.tm_sec = (int)(rem % 60L);
    g_tm_buf.tm_wday = (int)((days + 4L) % 7L);
    if (g_tm_buf.tm_wday < 0) g_tm_buf.tm_wday += 7;

    int year = 1970;
    while (days >= (is_leap_year(year) ? 366 : 365)) {
        days -= is_leap_year(year) ? 366 : 365;
        year++;
    }
    while (days < 0) {
        year--;
        days += is_leap_year(year) ? 366 : 365;
    }
    g_tm_buf.tm_year = year - 1900;
    g_tm_buf.tm_yday = (int)days;

    int mon = 0;
    while (mon < 12) {
        int dim = mdays[mon] + ((mon == 1 && is_leap_year(year)) ? 1 : 0);
        if (days < dim) break;
        days -= dim;
        mon++;
    }
    g_tm_buf.tm_mon = mon;
    g_tm_buf.tm_mday = (int)days + 1;
    g_tm_buf.tm_isdst = 0;
    return &g_tm_buf;
}

struct tm* localtime(const time_t* timep) {
    return gmtime(timep);
}

struct tm* gmtime_r(const time_t* timep, struct tm* result) {
    struct tm* t = gmtime(timep);
    if (!t || !result) return (void*)0;
    *result = *t;
    return result;
}

struct tm* localtime_r(const time_t* timep, struct tm* result) {
    return gmtime_r(timep, result);
}

char* asctime(const struct tm* tm) {
    static char buf[32];
    if (!tm) return (void*)0;
    strftime(buf, sizeof(buf), "%a %b %d %H:%M:%S %Y\n", tm);
    return buf;
}

char* ctime(const time_t* timep) {
    return asctime(localtime(timep));
}

time_t mktime(struct tm* tm) {
    if (!tm) return (time_t)-1;
    static const int mdays[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int year = tm->tm_year + 1900;
    long days = 0;
    for (int y = 1970; y < year; y++) {
        days += is_leap_year(y) ? 366 : 365;
    }
    for (int m = 0; m < tm->tm_mon && m < 12; m++) {
        days += mdays[m] + ((m == 1 && is_leap_year(year)) ? 1 : 0);
    }
    days += (tm->tm_mday - 1);
    return days * 86400L + tm->tm_hour * 3600L + tm->tm_min * 60L + tm->tm_sec;
}

static size_t fmt_append_str(char* dst, size_t max, size_t pos, const char* src) {
    while (*src && pos + 1 < max) {
        dst[pos++] = *src++;
    }
    return pos;
}

static size_t fmt_append_num(char* dst, size_t max, size_t pos, int val, int digits) {
    char tmp[16];
    int neg = 0;
    if (val < 0) { neg = 1; val = -val; }
    int n = 0;
    do {
        tmp[n++] = (char)('0' + (val % 10));
        val /= 10;
    } while (val > 0 && n < 15);
    while (n < digits && n < 15) tmp[n++] = '0';
    if (neg && pos + 1 < max) dst[pos++] = '-';
    while (n > 0 && pos + 1 < max) dst[pos++] = tmp[--n];
    return pos;
}

size_t strftime(char* s, size_t max, const char* format, const struct tm* tm) {
    if (!s || max == 0 || !format || !tm) return 0;
    static const char* const mon_abbr[12] = {
        "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"
    };
    static const char* const wday_abbr[7] = {
        "Sun","Mon","Tue","Wed","Thu","Fri","Sat"
    };
    size_t pos = 0;
    for (const char* p = format; *p && pos + 1 < max; p++) {
        if (*p != '%') {
            s[pos++] = *p;
            continue;
        }
        p++;
        if (!*p) break;
        switch (*p) {
            case 'Y': pos = fmt_append_num(s, max, pos, tm->tm_year + 1900, 4); break;
            case 'y': pos = fmt_append_num(s, max, pos, (tm->tm_year + 1900) % 100, 2); break;
            case 'm': pos = fmt_append_num(s, max, pos, tm->tm_mon + 1, 2); break;
            case 'd': pos = fmt_append_num(s, max, pos, tm->tm_mday, 2); break;
            case 'H': pos = fmt_append_num(s, max, pos, tm->tm_hour, 2); break;
            case 'M': pos = fmt_append_num(s, max, pos, tm->tm_min, 2); break;
            case 'S': pos = fmt_append_num(s, max, pos, tm->tm_sec, 2); break;
            case 'T':
                pos = fmt_append_num(s, max, pos, tm->tm_hour, 2);
                if (pos + 1 < max) s[pos++] = ':';
                pos = fmt_append_num(s, max, pos, tm->tm_min, 2);
                if (pos + 1 < max) s[pos++] = ':';
                pos = fmt_append_num(s, max, pos, tm->tm_sec, 2);
                break;
            case 'F':
                pos = fmt_append_num(s, max, pos, tm->tm_year + 1900, 4);
                if (pos + 1 < max) s[pos++] = '-';
                pos = fmt_append_num(s, max, pos, tm->tm_mon + 1, 2);
                if (pos + 1 < max) s[pos++] = '-';
                pos = fmt_append_num(s, max, pos, tm->tm_mday, 2);
                break;
            case 'b': case 'h':
                pos = fmt_append_str(s, max, pos, (tm->tm_mon >= 0 && tm->tm_mon < 12) ? mon_abbr[tm->tm_mon] : "???");
                break;
            case 'a':
                pos = fmt_append_str(s, max, pos, (tm->tm_wday >= 0 && tm->tm_wday < 7) ? wday_abbr[tm->tm_wday] : "???");
                break;
            case 'z': pos = fmt_append_str(s, max, pos, "+0000"); break;
            case 'Z': pos = fmt_append_str(s, max, pos, "UTC"); break;
            case '%': s[pos++] = '%'; break;
            default:  s[pos++] = *p; break;
        }
    }
    s[pos] = '\0';
    return pos;
}
