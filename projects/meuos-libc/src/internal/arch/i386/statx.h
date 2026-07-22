#ifndef MEUOS_I386_STATX_H
#define MEUOS_STATX_H

/* statx 转换辅助：把 statx 返回的结构转换成本库的 struct stat。
 *
 * - i386: legacy stat/stat64/fstat/lstat (4/5/6, 106/107/108) 返回 32 位
 *   time_t，与本库 struct stat（64 位 time_t via struct timespec）不兼容，
 *   改用 statx(i386 syscall 383) 取 64 位时间戳后转换。
 * - aarch64: kernel stat 布局（st_mode/st_nlink 顺序、padding）与
 *   struct stat 不同，同样用 statx（aarch64 syscall 291）+ 本转换函数。
 *
 * 头文件路径名带 i386 是历史原因；aarch64 直接复用本文件，转换逻辑
 * 架构无关。各 stat 包装器根据架构选用对应的内部 syscall 号。 */

#include <errno.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>

/* Linux UAPI constants we need but don't expose publicly. */
#define MEUOS_AT_FDCWD           (-100)
#define MEUOS_AT_EMPTY_PATH      0x1000
#define MEUOS_AT_SYMLINK_NOFOLLOW 0x100
#define MEUOS_STATX_BASIC_STATS  0x7ff

/* i386 statx syscall number (x86_64 is 332; this header is i386-only). */
#define LINUX_SYS_STATX_I386     383

/* aarch64 走 x86_64 内部号 332，由 syscall.h 翻译表转 aarch64 291。 */
#if defined(__aarch64__)
#define LINUX_SYS_STATX          332
#endif

struct meuos_statx_timestamp {
	int64_t tv_sec;
	uint32_t tv_nsec;
	int32_t __reserved;
};

struct meuos_statx {
	uint32_t stx_mask;
	uint32_t stx_blksize;
	uint64_t stx_attributes;
	uint32_t stx_nlink;
	uint32_t stx_uid;
	uint32_t stx_gid;
	uint16_t stx_mode;
	uint16_t __spare0[1];
	uint64_t stx_ino;
	uint64_t stx_size;
	uint64_t stx_blocks;
	uint64_t stx_attributes_mask;
	struct meuos_statx_timestamp stx_atime;
	struct meuos_statx_timestamp stx_btime;
	struct meuos_statx_timestamp stx_ctime;
	struct meuos_statx_timestamp stx_mtime;
	uint32_t stx_rdev_major;
	uint32_t stx_rdev_minor;
	uint32_t stx_dev_major;
	uint32_t stx_dev_minor;
	uint64_t stx_mnt_id;
	uint32_t stx_dio_mem_align;
	uint32_t stx_dio_offset_align;
	uint64_t __spare3[12];
};

/* Encode major/minor as the kernel's legacy dev_t so values match
 * what x86_64 fstat() produces.  Matches glibc's __makedev64 form
 * used by statx consumers that downgrade to struct stat. */
static inline dev_t
meuos_makedev_legacy(unsigned major, unsigned minor)
{
	return ((major & 0xfff) << 8) | (minor & 0xff);
}

static inline int
meuos_statx_to_stat(const struct meuos_statx *sx, struct stat *st)
{
	st->st_dev = meuos_makedev_legacy(sx->stx_dev_major, sx->stx_dev_minor);
	st->st_ino = (ino_t)sx->stx_ino;
	st->st_nlink = (nlink_t)sx->stx_nlink;
	st->st_mode = (mode_t)sx->stx_mode;
	st->st_uid = (uid_t)sx->stx_uid;
	st->st_gid = (gid_t)sx->stx_gid;
	st->st_rdev = meuos_makedev_legacy(sx->stx_rdev_major, sx->stx_rdev_minor);
	st->st_size = (off_t)sx->stx_size;
	st->st_blksize = (blksize_t)sx->stx_blksize;
	st->st_blocks = (blkcnt_t)sx->stx_blocks;
	st->st_atim.tv_sec = (time_t)sx->stx_atime.tv_sec;
	st->st_atim.tv_nsec = (long)sx->stx_atime.tv_nsec;
	st->st_mtim.tv_sec = (time_t)sx->stx_mtime.tv_sec;
	st->st_mtim.tv_nsec = (long)sx->stx_mtime.tv_nsec;
	st->st_ctim.tv_sec = (time_t)sx->stx_ctime.tv_sec;
	st->st_ctim.tv_nsec = (long)sx->stx_ctime.tv_nsec;
	return 0;
}

#endif
