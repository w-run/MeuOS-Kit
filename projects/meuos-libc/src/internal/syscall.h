#ifndef MEUOS_INTERNAL_SYSCALL_H
#define MEUOS_INTERNAL_SYSCALL_H

/* The raw syscall entry is implemented per-arch in src/internal/arch/<arch>/syscall.S.
 * It must NOT share a name with the inline wrappers below: mcc does not
 * inline static-inline functions, so a shared name would make the wrapper
 * recurse into itself instead of calling the assembly entry. */
long __meuos_syscall6(long, long, long, long, long, long, long);

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
	case 231: return 252; /* exit_group */
	case 217: return 220; /* getdents64 */
	case 228: return 265; /* clock_gettime */
	case 13:  return 174; /* rt_sigaction */
	case 14:  return 175; /* rt_sigprocmask */
	case 127: return 176; /* rt_sigpending */
	case 130: return 179; /* rt_sigsuspend */
	case 131: return 186; /* sigaltstack */
	case 62:  return 37;  /* kill */
	case 234: return 270; /* tgkill */
	default: return number;
	}
#elif defined(__aarch64__) || defined(__riscv)
	/* aarch64 使用 asm-generic syscall 号表，与 x86_64 完全不同。这里把
	 * 包装器使用的 x86_64 内部号翻译成 aarch64 原生号。aarch64 不存在
	 * 的旧 syscall（open/stat/mkdir/link/unlink/chmod/access/...）由各
	 * 包装器内的 #ifdef __aarch64__ 分支改走对应的 *at 变体，不会进入
	 * 本表；default 分支保留原号仅供未覆盖的内部号兜底。 */
	switch (number) {
	case 0:   return 63;   /* read */
	case 1:   return 64;   /* write */
	case 3:   return 57;   /* close */
	case 8:   return 62;   /* lseek */
	case 9:   return 222;  /* mmap */
	case 11:  return 215;  /* munmap */
	case 12:  return 214;  /* brk */
	case 13:  return 134;  /* rt_sigaction */
	case 14:  return 135;  /* rt_sigprocmask */
	case 16:  return 29;   /* ioctl */
	case 24:  return 124;  /* sched_yield */
	case 32:  return 23;   /* dup */
	case 35:  return 101;  /* nanosleep */
	case 39:  return 172;  /* getpid */
	case 41:  return 198;  /* socket */
	case 42:  return 203;  /* connect */
	case 49:  return 200;  /* bind */
	case 50:  return 201;  /* listen */
	case 56:  return 220;  /* clone（fork.c 的 aarch64 分支用） */
	case 59:  return 221;  /* execve */
	case 60:  return 93;   /* exit */
	case 61:  return 260;  /* wait4 */
	case 62:  return 129;  /* kill */
	case 72:  return 25;   /* fcntl */
	case 73:  return 32;   /* flock */
	case 79:  return 17;   /* getcwd */
	case 80:  return 49;   /* chdir */
	case 81:  return 50;   /* fchdir */
	case 91:  return 52;   /* fchmod */
	case 93:  return 55;   /* fchown */
	case 95:  return 166;  /* umask */
	case 96:  return 169;  /* gettimeofday */
	case 100: return 153;  /* times */
	case 102: return 174;  /* getuid */
	case 104: return 176;  /* getgid */
	case 107: return 175;  /* geteuid */
	case 108: return 177;  /* getegid */
	case 127: return 136;  /* rt_sigpending */
	case 130: return 133;  /* rt_sigsuspend */
	case 131: return 132;  /* sigaltstack */
	case 186: return 178;  /* gettid */
	case 202: return 98;   /* futex */
	case 217: return 61;   /* getdents64 */
	case 228: return 113;  /* clock_gettime */
	case 231: return 94;   /* exit_group */
	case 234: return 131;  /* tgkill */
	/* *at 变体：aarch64 包装器分支通过这些 x86_64 内部号调用，翻译到
	 * 对应的 aarch64 asm-generic 号。 */
	case 257: return 56;   /* openat */
	case 258: return 34;   /* mkdirat */
	case 260: return 54;   /* fchownat */
	case 262: return 79;   /* newfstatat */
	case 263: return 35;   /* unlinkat */
	case 264: return 38;   /* renameat */
	case 265: return 37;   /* linkat */
	case 266: return 36;   /* symlinkat */
	case 267: return 78;   /* readlinkat */
	case 268: return 53;   /* fchmodat */
	case 269: return 48;   /* faccessat */
	case 292: return 24;   /* dup3 */
	case 293: return 59;   /* pipe2 */
	case 332: return 291;  /* statx（stat/lstat/fstat 的 aarch64 分支用） */
	case 412: return 88;   /* utimensat（utime.c 的 aarch64 分支复用 i386 路径） */
	default: return number;
	}
#else
	return number;
#endif
}

static inline long
__syscall6(long number, long first, long second, long third, long fourth, long fifth, long sixth)
{
	return __meuos_syscall6(__syscall_number(number), first, second, third, fourth, fifth, sixth);
}

static inline long
__syscall0(long number)
{
	return __meuos_syscall6(__syscall_number(number), 0, 0, 0, 0, 0, 0);
}

static inline long
__syscall3(long number, long first, long second, long third)
{
	return __meuos_syscall6(__syscall_number(number), first, second, third, 0, 0, 0);
}

static inline long
__syscall4(long number, long first, long second, long third, long fourth)
{
	return __meuos_syscall6(__syscall_number(number), first, second, third, fourth, 0, 0);
}

static inline long
__syscall1(long number, long first)
{
	return __meuos_syscall6(__syscall_number(number), first, 0, 0, 0, 0, 0);
}

static inline long
__syscall2(long number, long first, long second)
{
	return __meuos_syscall6(__syscall_number(number), first, second, 0, 0, 0, 0);
}

static inline int
__syscall_error(long result)
{
	return result < 0 && result >= -4095;
}

#endif
