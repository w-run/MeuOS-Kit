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
	case 10: return 125; /* mprotect */
	case 11: return 91; /* munmap */
	case 12: return 45; /* brk */
	case 21: return 33; /* access */
	case 22: return 42; /* pipe */
	case 23: return 82; /* select */
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
	case 63:  return 122; /* uname */
	case 234: return 270; /* tgkill */
	/* 向量 / 定位 I/O + *at / pipe / dup 扩展（i386 32 位号）。 */
	case 19:  return 145;  /* readv */
	case 20:  return 146;  /* writev */
	case 17:  return 180;  /* pread */
	case 18:  return 181;  /* pwrite */
	case 295: return 333;  /* preadv */
	case 296: return 334;  /* pwritev */
	case 257: return 295;  /* openat */
	case 292: return 330;  /* dup3 */
	case 293: return 354;  /* pipe2 */
	/* 文件同步：sync/fsync/fdatasync（i386 独立号）。 */
	case 162: return 36;   /* sync */
	case 74:  return 118;  /* fsync */
	case 75:  return 148;  /* fdatasync */
	/* 调度优先级（i386 独立号）。 */
	case 140: return 96;   /* getpriority */
	case 141: return 97;   /* setpriority */
	/* 间隔定时器：getitimer/setitimer（i386 独立号）。 */
	case 36:  return 105;  /* getitimer */
	case 38:  return 104;  /* setitimer */
	/* 进程组 / 会话（i386 独立号）。 */
	case 109: return 57;   /* setpgid */
	case 121: return 132;  /* getpgid */
	case 112: return 66;   /* setsid */
	case 124: return 147;  /* getsid */
	/* 进程身份设置（i386 独立号）。 */
	case 105: return 23;   /* setuid */
	case 106: return 46;   /* setgid */
	case 113: return 70;   /* setreuid */
	case 114: return 71;   /* setregid */
	/* 进程身份读取（i386 独立号）。 */
	case 110: return 64;   /* getppid */
	/* 资源使用统计（i386 独立号）。 */
	case 98:  return 77;   /* getrusage */
	/* 文件截断（i386 独立号）。 */
	case 76:  return 92;   /* truncate */
	case 77:  return 93;   /* ftruncate */
	default: return number;
	}
#elif defined(__arm__)
	/* ARM EABI (armv7) syscall 号。内部号是 x86_64 稳定号，这里翻译成
	 * ARM 原生号（来自 Linux arch/arm/tools/syscall.tbl / gdb arm-linux.xml）。
	 * 低段 (0-131) 与 i386 大多数相同，差异项：getdents64(220→217)、
	 * clock_gettime(265→263)、exit_group(252→248)、mmap 用 mmap2(192)。
	 * 注意：ARM 的 rt_sig* 段是 174-179（与 i386 相同），不要误写成 mprotect(125) 等。 */
	switch (number) {
	case 0:   return 3;    /* read */
	case 1:   return 4;    /* write */
	case 2:   return 5;    /* open */
	case 3:   return 6;    /* close */
	case 4:   return 106;  /* stat (old) */
	case 5:   return 108;  /* fstat (old) */
	case 6:   return 107;  /* lstat (old) */
	case 8:   return 19;   /* lseek */
	case 9:   return 192;  /* mmap2 (page-offset, not byte) */
	case 10:  return 125;  /* mprotect */
	case 11:  return 91;   /* munmap */
	case 12:  return 45;   /* brk */
	case 13:  return 174;  /* rt_sigaction */
	case 14:  return 175;  /* rt_sigprocmask */
	case 16:  return 54;   /* ioctl */
	case 21:  return 33;   /* access */
	case 22:  return 42;   /* pipe */
	case 23:  return 142;  /* select */
	case 24:  return 158;  /* sched_yield */
	case 28:  return 220;  /* madvise */
	case 32:  return 41;   /* dup */
	case 33:  return 63;   /* dup2 */
	case 35:  return 162;  /* nanosleep */
	case 39:  return 20;   /* getpid */
	case 56:  return 120;  /* clone */
	case 57:  return 2;    /* fork */
	case 59:  return 11;   /* execve */
	case 60:  return 1;    /* exit */
	case 61:  return 114;  /* wait4 */
	case 62:  return 37;   /* kill */
	case 63:  return 122;  /* uname */
	case 72:  return 55;   /* fcntl */
	case 79:  return 183;  /* getcwd */
	case 80:  return 12;   /* chdir */
	case 81:  return 13;   /* fchdir */
	case 82:  return 38;   /* rename */
	case 83:  return 39;   /* mkdir */
	case 84:  return 40;   /* rmdir */
	case 86:  return 9;    /* link */
	case 87:  return 10;   /* unlink */
	case 88:  return 83;   /* symlink */
	case 89:  return 85;   /* readlink */
	case 90:  return 15;   /* chmod */
	case 91:  return 94;   /* fchmod */
	case 93:  return 95;   /* fchown */
	case 95:  return 60;   /* umask */
	case 96:  return 169;  /* gettimeofday */
	case 100: return 153;  /* times */
	case 102: return 24;   /* getuid */
	case 104: return 47;   /* getgid */
	case 107: return 49;   /* geteuid */
	case 108: return 50;   /* getegid */
	case 110: return 64;   /* getppid */
	/* 资源使用统计（与 i386 相同）。 */
	case 98:  return 77;   /* getrusage */
	/* 文件截断（与 i386 相同）。 */
	case 76:  return 92;   /* truncate */
	case 77:  return 93;   /* ftruncate */
	case 127: return 176;  /* rt_sigpending */
	case 130: return 179;  /* rt_sigsuspend */
	case 131: return 186;  /* sigaltstack */
	case 186: return 224;  /* gettid */
	case 202: return 240;  /* futex */
	case 217: return 217;  /* getdents64 */
	case 228: return 263;  /* clock_gettime */
	case 231: return 248;  /* exit_group */
	case 234: return 268;  /* tgkill */
	case 257: return 322;  /* openat */
	case 258: return 323;  /* mkdirat */
	case 259: return 324;  /* mknodat */
	case 260: return 325;  /* fchownat */
	case 262: return 327;  /* newfstatat / fstatat64 */
	case 263: return 328;  /* unlinkat */
	case 264: return 329;  /* renameat */
	case 265: return 330;  /* linkat */
	case 266: return 331;  /* symlinkat */
	case 267: return 332;  /* readlinkat */
	case 268: return 333;  /* fchmodat */
	case 269: return 334;  /* faccessat */
	case 292: return 358;  /* dup3 */
	case 293: return 359;  /* pipe2 */
	case 302: return 369;  /* prlimit64 */
	case 332: return 383;  /* statx */
	case 412: return 348;  /* utimensat */
	/* 向量 / 定位 I/O（ARM 32 位号）。 */
	case 19:  return 145;  /* readv */
	case 20:  return 146;  /* writev */
	case 17:  return 180;  /* pread */
	case 18:  return 181;  /* pwrite */
	case 295: return 333;  /* preadv */
	case 296: return 334;  /* pwritev */
	/* 文件同步：sync/fsync/fdatasync（与 i386 相同，见行前注释）。 */
	case 162: return 36;   /* sync */
	case 74:  return 118;  /* fsync */
	case 75:  return 148;  /* fdatasync */
	/* 调度优先级（与 i386 相同）。 */
	case 140: return 96;   /* getpriority */
	case 141: return 97;   /* setpriority */
	/* 间隔定时器（与 i386 相同）。 */
	case 36:  return 105;  /* getitimer */
	case 38:  return 104;  /* setitimer */
	/* 进程组 / 会话（与 i386 相同）。 */
	case 109: return 57;   /* setpgid */
	case 121: return 132;  /* getpgid */
	case 112: return 66;   /* setsid */
	case 124: return 147;  /* getsid */
	/* 进程身份设置（与 i386 相同）。 */
	case 105: return 23;   /* setuid */
	case 106: return 46;   /* setgid */
	case 113: return 70;   /* setreuid */
	case 114: return 71;   /* setregid */
	default: return number;
	}
#elif defined(__aarch64__) || defined(__riscv) || defined(__loongarch64)
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
	case 10:  return 226;  /* mprotect */
	case 11:  return 215;  /* munmap */
	case 12:  return 214;  /* brk */
	case 13:  return 134;  /* rt_sigaction */
	case 14:  return 135;  /* rt_sigprocmask */
	case 16:  return 29;   /* ioctl */
	case 23:  return 72;   /* pselect6（asm-generic 无 select，select.c 走 pselect6） */
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
	case 63:  return 160;  /* uname */
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
	case 110: return 173;  /* getppid */
	/* 资源使用统计：asm-generic 号。 */
	case 98:  return 165;  /* getrusage */
	/* 文件截断：asm-generic 号（__NR3264_*，64-bit off_t）。 */
	case 76:  return 45;   /* truncate */
	case 77:  return 46;   /* ftruncate */
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
	/* 向量 / 定位 I/O：asm-generic 自有 readv/writev/pread64/pwrite64 号。 */
	case 19:  return 65;   /* readv */
	case 20:  return 66;   /* writev */
	case 17:  return 67;   /* pread64（pread） */
	case 18:  return 68;   /* pwrite64（pwrite） */
	case 295: return 69;   /* preadv */
	case 296: return 70;   /* pwritev */
	/* 文件同步：asm-generic 号。 */
	case 162: return 81;   /* sync */
	case 74:  return 82;   /* fsync */
	case 75:  return 83;   /* fdatasync */
	/* 调度优先级：asm-generic 与 x86_64 内部号相反。
	 * 内部 getpriority=140 → 原生 141；内部 setpriority=141 → 原生 140。 */
	case 140: return 141;  /* getpriority */
	case 141: return 140;  /* setpriority */
	/* 间隔定时器：asm-generic 号（getitimer=102, setitimer=103）。 */
	case 36:  return 102;  /* getitimer */
	case 38:  return 103;  /* setitimer */
	/* 进程组 / 会话：asm-generic 号。 */
	case 109: return 154;  /* setpgid */
	case 121: return 155;  /* getpgid */
	case 112: return 157;  /* setsid */
	case 124: return 156;  /* getsid */
	/* 进程身份设置：asm-generic 号（有独立 setuid/setgid/setreuid/
	 * setregid；seteuid/isetegid 在 uid.c 用 setreuid/setregid 表达）。 */
	case 105: return 146;  /* setuid */
	case 106: return 144;  /* setgid */
	case 113: return 145;  /* setreuid */
	case 114: return 143;  /* setregid */
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
__syscall5(long number, long first, long second, long third, long fourth, long fifth)
{
	return __meuos_syscall6(__syscall_number(number), first, second, third, fourth, fifth, 0);
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
