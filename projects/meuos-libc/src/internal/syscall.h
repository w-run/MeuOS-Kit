#ifndef MEUOS_INTERNAL_SYSCALL_H
#define MEUOS_INTERNAL_SYSCALL_H

/* x86_64 System V function ABI wrapper around the Linux syscall ABI. */
long __syscall6(long, long, long, long, long, long, long);

/* Syscall wrappers use the x86_64 numbers as stable internal identifiers.
 * Translate the small i386 bootstrap subset here; calls whose ABI shape
 * differs (notably mmap and socketcall) are added only with dedicated
 * wrappers rather than silently issuing a wrong syscall. */
static inline long
__syscall_number(long number)
{
#if defined(__i386__)
	switch (number) {
	case 0:  return 3;  /* read */
	case 1:  return 4;  /* write */
	case 2:  return 5;  /* open */
	case 3:  return 6;  /* close */
	case 4:  return 106; /* stat */
	case 5:  return 108; /* fstat */
	case 6:  return 107; /* lstat */
	case 8:  return 19; /* lseek */
	case 11: return 91; /* munmap */
	case 12: return 45; /* brk */
	case 21: return 33; /* access */
	case 22: return 42; /* pipe */
	case 24: return 158; /* sched_yield */
	case 32: return 41; /* dup */
	case 33: return 63; /* dup2 */
	case 35: return 162; /* nanosleep */
	case 39: return 20; /* getpid */
	case 57: return 2;  /* fork */
	case 59: return 11; /* execve */
	case 60: return 1;  /* exit */
	case 61: return 114; /* wait4 */
	case 79: return 183; /* getcwd */
	case 80: return 12; /* chdir */
	case 82: return 38; /* rename */
	case 83: return 39; /* mkdir */
	case 84: return 40; /* rmdir */
	case 86: return 9;  /* link */
	case 87: return 10; /* unlink */
	case 88: return 83; /* symlink */
	case 89: return 85; /* readlink */
	case 90: return 15; /* chmod */
	case 186: return 224; /* gettid */
	case 202: return 240; /* futex */
	case 217: return 220; /* getdents64 */
	case 228: return 265; /* clock_gettime */
	default: return number;
	}
#else
	return number;
#endif
}

static inline long
__syscall0(long number)
{
	return __syscall6(__syscall_number(number), 0, 0, 0, 0, 0, 0);
}

static inline long
__syscall3(long number, long first, long second, long third)
{
	return __syscall6(__syscall_number(number), first, second, third, 0, 0, 0);
}

static inline long
__syscall4(long number, long first, long second, long third, long fourth)
{
	return __syscall6(__syscall_number(number), first, second, third, fourth, 0, 0);
}

static inline long
__syscall1(long number, long first)
{
	return __syscall6(__syscall_number(number), first, 0, 0, 0, 0, 0);
}

static inline long
__syscall2(long number, long first, long second)
{
	return __syscall6(__syscall_number(number), first, second, 0, 0, 0, 0);
}

static inline int
__syscall_error(long result)
{
	return result < 0 && result >= -4095;
}

#endif
