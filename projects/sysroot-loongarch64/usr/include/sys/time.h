#ifndef MEUOS_SYS_TIME_H
#define MEUOS_SYS_TIME_H

#include <sys/types.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* suseconds_t and struct timeval come from <sys/types.h> and <time.h>. */

struct timezone {
	int tz_minuteswest;
	int tz_dsttime;
};

int gettimeofday(struct timeval *, void *);
int settimeofday(const struct timeval *, const struct timezone *);
int utimes(const char *, const struct timeval[2]);

#define ITIMER_REAL 0
#define ITIMER_VIRTUAL 1
#define ITIMER_PROF 2

struct itimerval {
	struct timeval it_interval;
	struct timeval it_value;
};

int getitimer(int, struct itimerval *);
int setitimer(int, const struct itimerval *, struct itimerval *);

#ifdef __cplusplus
}
#endif

#endif
