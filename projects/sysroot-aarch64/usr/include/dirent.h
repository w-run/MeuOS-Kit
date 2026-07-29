#ifndef MEUOS_DIRENT_H
#define MEUOS_DIRENT_H

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct dirent {
	ino_t d_ino;
	unsigned short d_reclen;
	unsigned char d_type;
	char d_name[256];
};

#define DT_UNKNOWN  0
#define DT_FIFO     1
#define DT_CHR      2
#define DT_DIR      4
#define DT_BLK      6
#define DT_REG      8
#define DT_LNK     10
#define DT_SOCK    12
#define DT_WHT     14

typedef struct {
	int fd;
	struct dirent entry;
	char buffer[4096];
	size_t buf_pos;
	size_t buf_end;
} DIR;

DIR *opendir(const char *);
struct dirent *readdir(DIR *);
int closedir(DIR *);
long telldir(DIR *);
void seekdir(DIR *, long);
void rewinddir(DIR *);

#ifdef __cplusplus
}
#endif

#endif
