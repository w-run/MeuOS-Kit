#ifndef MEUOS_UNISTD_H
#define MEUOS_UNISTD_H

#include <features.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

__BEGIN_DECLS
ssize_t read(int, void *, size_t);
ssize_t write(int, const void *, size_t);
ssize_t pread(int, void *, size_t, off_t);
ssize_t pwrite(int, const void *, size_t, off_t);
int close(int);
int open(const char *, int, ...);
pid_t getpid(void);
pid_t getppid(void);
pid_t gettid(void);
pid_t fork(void);
int execve(const char *, char *const[], char *const[]);
int execv(const char *, char *const[]);
int execvp(const char *, char *const[]);
int pipe(int [2]);
int link(const char *, const char *);
int symlink(const char *, const char *);
ssize_t readlink(const char *, char *, size_t);
int access(const char *, int);
ssize_t getdents64(int, void *, size_t);
off_t lseek(int, off_t, int);
int chdir(const char *);
char *getcwd(char *, size_t);
int dup(int);
int dup2(int, int);
int dup3(int, int, int);
int pipe2(int [2], int);
#ifndef AT_FDCWD
#define AT_FDCWD (-100)
#endif
int unlink(const char *);
int rmdir(const char *);
int rename(const char *, const char *);
int mkdir(const char *, mode_t);
int brk(void *);
void *sbrk(intptr_t);
_Noreturn void _exit(int);

/* getopt() command-line option parsing (POSIX.1-2008). */
extern char *optarg;
extern int optind;
extern int opterr;
extern int optopt;
int getopt(int, char * const[], const char *);

/* Suspension / scheduling alarms (POSIX.1-2008). */
unsigned int sleep(unsigned int);
int usleep(unsigned int);
int pause(void);
unsigned int alarm(unsigned int);

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

pid_t wait(int *);

/* glibc 兼容：<unistd.h> 也声明 select()（fd_set 来自 <sys/types.h>）。
 * 完整定义见 <sys/select.h>。 */
#include <time.h>
int select(int, fd_set *, fd_set *, fd_set *, struct timeval *);

int fchmod(int, mode_t);
int fchown(int, uid_t, gid_t);
int chown(const char *, uid_t, gid_t);
uid_t getuid(void);
uid_t geteuid(void);
gid_t getgid(void);
gid_t getegid(void);
mode_t umask(mode_t);

/* Process groups / sessions (POSIX.1-2008). */
int setpgid(pid_t, pid_t);
pid_t getpgid(pid_t);
pid_t getpgrp(void);
pid_t setsid(void);
pid_t getsid(pid_t);
int isatty(int);
int fchdir(int);
void sync(void);
int fsync(int);
int fdatasync(int);
int nice(int);
long pathconf(const char *, int);
long fpathconf(int, int);
long sysconf(int);
size_t confstr(int, char *, size_t);

/* pathconf name values */
#define _PC_LINK_MAX 0
#define _PC_MAX_CANON 1
#define _PC_MAX_INPUT 2
#define _PC_NAME_MAX 3
#define _PC_PATH_MAX 4
#define _PC_PIPE_BUF 5
#define _PC_CHOWN_RESTRICTED 6
#define _PC_NO_TRUNC 7
#define _PC_VDISABLE 8

/* confstr name values */
#define _CS_PATH 1
#define _CS_GNU_LIBC_VERSION 2

/* sysconf name values */
#define _SC_ARG_MAX 0
#define _SC_CHILD_MAX 1
#define _SC_CLK_TCK 2
#define _SC_NGROUPS_MAX 3
#define _SC_OPEN_MAX 4
#define _SC_JOB_CONTROL 5
#define _SC_SAVED_IDS 6
#define _SC_VERSION 7
#define _SC_TZNAME 8
#define _SC_PAGESIZE 9
#define _SC_PAGE_SIZE _SC_PAGESIZE
#define _SC_PHYS_PAGES 85
#define _SC_GETPW_R_SIZE_MAX 68
#define _SC_ATEXIT_MAX 78
#define _SC_NPROCESSORS_ONLN 84

__END_DECLS

#endif /* MEUOS_UNISTD_H */
