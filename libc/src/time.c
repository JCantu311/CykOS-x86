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
