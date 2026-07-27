#ifndef MT_MSYS_H
#define MT_MSYS_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Magic for .msys file header */
#define MSYS_MAGIC "Msys1\0\0\0"
#define MSYS_MAGIC_LEN 8

/* Flags for msys_header.flags */
#define MSYS_F_NONE      0x00  /* no compression */
#define MSYS_F_ZLIB      0x01  /* zlib deflate (data blocks) */
#define MSYS_F_ZSTD      0x02  /* zstd compression (future) */
#define MSYS_F_INCREMENTAL 0x04  /* incremental mode (future) */

/* .msys file header (32 bytes on disk) */
struct msys_header {
	char     magic[8];       /* "Msys1\0\0\0" */
	uint64_t index_offset;   /* offset from file start to index block */
	uint32_t index_count;    /* number of entries in index */
	uint32_t flags;          /* flags */
	uint8_t  reserved[8];    /* reserved, must be zero */
};

/* Index entry (16 + name_len bytes on disk, variable-length name tail) */
struct msys_index_entry {
	uint8_t name_hash[4];    /* FNV-1a 32-bit, little-endian */
	uint8_t data_offset[6];  /* uint48 little-endian */
	uint8_t data_size[4];    /* uint32 little-endian */
	uint8_t name_len[2];     /* uint16 little-endian */
	/* name follows (name_len bytes, no NUL terminator) */
};

_Static_assert(sizeof(struct msys_header) == 32, "msys_header must be 32 bytes");
_Static_assert(sizeof(struct msys_index_entry) == 16, "msys_index_entry must be 16 bytes");

/* In-memory handle for an open .msys file */
struct msys {
	void   *base;            /* mmap base address */
	size_t  size;            /* file size */
	struct msys_header *hdr; /* pointer to header within mmap */
	struct msys_index_entry *index; /* pointer to first index entry */
	unsigned char **entries; /* private: per-entry pointers (variable len) */
};

/* Open a .msys file for reading.
 *   path: path to .msys file
 *   Returns a handle, or NULL on error (errno set). */
struct msys *msys_open(const char *path);

/* Look up a name in the index by binary search.
 *   m:     handle from msys_open
 *   name:  NUL-terminated path to search for
 *   size:  out parameter, receives data size on success
 *   Returns a pointer to the data within the mmap'd region, or NULL if not
 *   found (errno set to ENOENT on hash miss, or ENOMSG if hash collision
 *   causes an actual string mismatch). */
const void *msys_search(struct msys *m, const char *name, size_t *size);

/* Read data for name into user buffer.
 *   m:      handle from msys_open
 *   name:   NUL-terminated path to search for
 *   buf:    destination buffer
 *   buflen: maximum bytes to read
 *   Returns number of bytes copied on success, or -1 on error (errno set). */
int msys_read(struct msys *m, const char *name, void *buf, size_t buflen);

/* VFS: Open a file from within the .msys archive as a FILE* (fmemopen wrapper).
 *   path:  path within archive, e.g. "usr/include/stdio.h" (no leading /)
 *   mode:  fopen mode string (typically "r")
 *   Returns a FILE* on success, or NULL on error (errno set).
 *   If m is NULL or the path is not found, returns NULL (errno = ENOENT). */
FILE *msys_fopen(struct msys *m, const char *path, const char *mode);

/* VFS: Load entire file content from within the .msys archive into malloc'd memory.
 *   path:  path within archive
 *   buf:   out parameter, receives pointer to allocated memory (caller must free)
 *   size:  out parameter, receives number of bytes read (may be NULL)
 *   Returns number of bytes on success, or -1 on error (errno set). */
int   msys_load(struct msys *m, const char *path, void **buf, size_t *size);

/* Close a .msys handle, munmap the file, free memory.
 *   m: handle to close (NULL is safe). */
void msys_close(struct msys *m);

/* FNV-1a 32-bit hash helper.
 *   name: pointer to data to hash
 *   len:  number of bytes
 *   Returns 32-bit FNV-1a hash. */
uint32_t msys_fnv1a(const unsigned char *name, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* MT_MSYS_H */
