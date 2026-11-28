#ifndef _TIME_H
#define _TIME_H

#include <stddef.h>
#include <sys/types.h>

typedef long clock_t;

#define CLOCKS_PER_SEC 1000

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

struct timespec {
    time_t tv_sec;
    long tv_nsec;
};

clock_t clock(void);
time_t time(time_t* tloc);
double difftime(time_t time1, time_t time0);
struct tm* gmtime(const time_t* timep);
struct tm* localtime(const time_t* timep);
struct tm* gmtime_r(const time_t* timep, struct tm* result);
struct tm* localtime_r(const time_t* timep, struct tm* result);
char* asctime(const struct tm* tm);
char* ctime(const time_t* timep);
time_t mktime(struct tm* tm);
size_t strftime(char* s, size_t max, const char* format, const struct tm* tm);

#endif /* _TIME_H */
