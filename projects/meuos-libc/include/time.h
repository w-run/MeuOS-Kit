#ifndef MEUOS_TIME_H
#define MEUOS_TIME_H

#include <features.h>
#include <sys/types.h>
#include <sys/times.h>

struct timespec { time_t tv_sec; long tv_nsec; };
struct timeval { time_t tv_sec; suseconds_t tv_usec; };
struct tm { int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst; };

#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
#define CLOCKS_PER_SEC 100

__BEGIN_DECLS
time_t time(time_t *);
int nanosleep(const struct timespec *, struct timespec *);
int clock_gettime(clockid_t, struct timespec *);

struct tm *gmtime(const time_t *);
struct tm *gmtime_r(const time_t *, struct tm *);
struct tm *localtime(const time_t *);
struct tm *localtime_r(const time_t *, struct tm *);
time_t mktime(struct tm *);
size_t strftime(char *, size_t, const char *, const struct tm *);
char *asctime(const struct tm *);
char *ctime(const time_t *);
char *strptime(const char *restrict, const char *restrict, struct tm *restrict);
double difftime(time_t, time_t);
clock_t clock(void);
__END_DECLS

#endif
