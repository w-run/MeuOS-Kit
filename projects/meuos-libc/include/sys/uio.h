#ifndef MEUOS_SYS_UIO_H
#define MEUOS_SYS_UIO_H

#include <sys/types.h>

typedef struct iovec {
	void  *iov_base;    /* starting address of buffer */
	size_t iov_len;     /* number of bytes to transfer */
} iovec;

ssize_t readv(int, const struct iovec *, int);
ssize_t writev(int, const struct iovec *, int);
ssize_t preadv(int, const struct iovec *, int, off_t);
ssize_t pwritev(int, const struct iovec *, int, off_t);

#endif /* MEUOS_SYS_UIO_H */
