#ifndef MEUOS_TIME_H
#define MEUOS_TIME_H

#include <features.h>
#include <sys/types.h>
#include <sys/times.h>

struct timespec { time_t tv_sec; long tv_nsec; };
struct itimerspec { struct timespec it_interval; struct timespec it_value; };
typedef int timer_t;
struct timeval { time_t tv_sec; suseconds_t tv_usec; };
struct tm { int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst; };

#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
#define CLOCKS_PER_SEC 100

__BEGIN_DECLS
time_t time(time_t *);
int nanosleep(const struct timespec *, struct timespec *);
int clock_gettime(clockid_t, struct timespec *);
int clock_getres(clockid_t, struct timespec *);
int clock_getres(clockid_t, struct timespec *);

struct tm *gmtime(const time_t *);
struct tm *gmtime_r(const time_t *, struct tm *);
struct tm *localtime(const time_t *);
struct tm *localtime_r(const time_t *, struct tm *);
time_t mktime(struct tm *);
time_t timegm(struct tm *);
time_t timelocal(struct tm *);
size_t strftime(char *, size_t, const char *, const struct tm *);
char *asctime(const struct tm *);
char *ctime(const time_t *);
char *strptime(const char *restrict, const char *restrict, struct tm *restrict);
double difftime(time_t, time_t);
/* C23 7.27.1: TIME_UTC values for timespec_get/timespec_getres. */
#define TIME_UTC 1

clock_t clock(void);

/* ISO C23 7.27.2.5: get the resolution of a clock.  Returns 0 on success,
 * or an implementation-defined negative value representing the error. */
int timespec_getres(struct timespec *ts, int base);

/* POSIX timers (C11 7.27 / POSIX.1-2008). */
int clock_settime(clockid_t, const struct timespec *);
int clock_nanosleep(clockid_t, int, const struct timespec *, struct timespec *);

/* timer_t is an int (kernel timerfd).  struct sigevent is declared in
 * <signal.h> for the full POSIX form; this is the minimal forward decl
 * so <time.h> alone compiles. */
struct sigevent;
int timer_create(clockid_t, struct sigevent *restrict, timer_t *restrict);
int timer_settime(timer_t, int, const struct itimerspec *restrict,
                  struct itimerspec *restrict);
int timer_gettime(timer_t, struct itimerspec *);
int timer_getoverrun(timer_t);
int timer_delete(timer_t);

__END_DECLS

#endif
