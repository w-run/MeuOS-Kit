#ifndef MEUOS_TIME_H
#define MEUOS_TIME_H

#include <sys/types.h>

struct timespec { time_t tv_sec; long tv_nsec; };
struct timeval { time_t tv_sec; suseconds_t tv_usec; };
struct tm { int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst; };

#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1

time_t time(time_t *);
int nanosleep(const struct timespec *, struct timespec *);
int clock_gettime(clockid_t, struct timespec *);

struct tm *gmtime(const time_t *);
struct tm *gmtime_r(const time_t *, struct tm *);
struct tm *localtime(const time_t *);
struct tm *localtime_r(const time_t *, struct tm *);
time_t mktime(struct tm *);
size_t strftime(char *, size_t, const char *, const struct tm *);

#endif
