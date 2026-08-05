#ifndef _FTW_H
#define _FTW_H

#include <features.h>
#include <sys/stat.h>

/* Values for the second argument of the nftw(fileop) function. */
#define FTW_F   0   /* regular file or file that could not be classified */
#define FTW_D   1   /* directory */
#define FTW_DNR 2   /* directory that cannot be read */
#define FTW_NS  3   /* file that has no stat info (lstat failed) */
#define FTW_SL  4   /* symbolic link (only with FTW_PHYS) */
#define FTW_DP  5   /* directory, post-order (only with FTW_DEPTH) */
#define FTW_SLN 6   /* dangling symbolic link (only without FTW_PHYS) */

/* Flags for the third argument of nftw. */
#define FTW_PHYS  1 /* follow symbolic links (do not chdir/walk symlinks as dirs) */
#define FTW_MOUNT 2 /* do not leave the filesystem the root resides on */
#define FTW_CHDIR 4 /* chdir() to each directory before visiting it */
#define FTW_DEPTH 8 /* visit directory post-order */

struct FTW {
	int base;  /* offset into pathname of the filename part */
	int level; /* depth relative to the initial path */
};

/* The nftw callback: fn(path, &st, ftw_flag, &ftw_base). */
typedef int (*__nftw_fn_t)(const char *, const struct stat *, int, struct FTW *);

__BEGIN_DECLS
int nftw(const char *, __nftw_fn_t, int, int);
__END_DECLS

#endif
