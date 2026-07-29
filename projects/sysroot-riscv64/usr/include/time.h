#ifndef MEUOS_TIME_H
#define MEUOS_TIME_H

#include <sys/types.h>

struct timespec { time_t tv_sec; long tv_nsec; };
struct timeval { time_t tv_sec; suseconds_t tv_usec; };

#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1

time_t time(time_t *);
int nanosleep(const struct timespec *, struct timespec *);
int clock_gettime(clockid_t, struct timespec *);

#endif
