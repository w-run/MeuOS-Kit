#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "../internal/syscall.h"

/* Linux x86_64 syscall number for getdents64. */
#define LINUX_SYS_GETDENTS64 217

struct linux_dirent64 {
	unsigned long d_ino;
	unsigned long d_off;
	unsigned short d_reclen;
	unsigned char d_type;
	char d_name[];
};

DIR *
opendir(const char *name)
{
	int fd = open(name, O_RDONLY | O_DIRECTORY);

	if (fd < 0)
		return NULL;
	DIR *dir = calloc(1, sizeof(*dir));

	if (!dir) {
		close(fd);
		return NULL;
	}
	dir->fd = fd;
	dir->buf_pos = 0;
	dir->buf_end = 0;
	return dir;
}

struct dirent *
readdir(DIR *dir)
{
	if (!dir)
		return NULL;
	if (dir->buf_pos >= dir->buf_end) {
		long count = __syscall3(LINUX_SYS_GETDENTS64, dir->fd,
		    (long)dir->buffer, sizeof(dir->buffer));

		if (__syscall_error(count)) {
			errno = (int)-count;
			return NULL;
		}
		if (count == 0)
			return NULL;
		dir->buf_pos = 0;
		dir->buf_end = (size_t)count;
	}
	{
		struct linux_dirent64 *src =
		    (struct linux_dirent64 *)(dir->buffer + dir->buf_pos);

		dir->entry.d_ino = (ino_t)src->d_ino;
		dir->entry.d_reclen = src->d_reclen;
		dir->entry.d_type = src->d_type;
		strncpy(dir->entry.d_name, src->d_name,
		    sizeof(dir->entry.d_name) - 1);
		dir->entry.d_name[sizeof(dir->entry.d_name) - 1] = '\0';
		dir->buf_pos += src->d_reclen;
	}
	return &dir->entry;
}

int
closedir(DIR *dir)
{
	int r;

	if (!dir) {
		errno = EBADF;
		return -1;
	}
	r = close(dir->fd);
	free(dir);
	return r;
}

long
telldir(DIR *dir)
{
	return (long)dir->buf_pos;
}

void
seekdir(DIR *dir, long loc)
{
	if (dir)
		dir->buf_pos = (size_t)loc;
}

void
rewinddir(DIR *dir)
{
	if (dir) {
		dir->buf_pos = 0;
		dir->buf_end = 0;
	}
}
