#ifndef MT_MSYS_H
#define MT_MSYS_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Magic for .msys file header */
#define MSYS_MAGIC    "Msys1\0\0\0"
#define MSYS_MAGIC_V2 "Msys2\0\0\0"
#define MSYS_MAGIC_LEN 8

/* Format versions */
#define MSYS_FORMAT_V1 1
#define MSYS_FORMAT_V2 2

/* Flags for msys_header.flags */
#define MSYS_F_NONE      0x00    /* no compression */
#define MSYS_F_ZLIB      0x01    /* zlib deflate (data blocks) */
#define MSYS_F_ZSTD      0x02    /* zstd compression */
#define MSYS_F_INCREMENTAL 0x04  /* incremental mode */
#define MSYS_F_DEDUP     0x0100  /* v2: content dedup */
#define MSYS_F_SIGNED    0x0200  /* v2: has signature extension */
#define MSYS_F_DIR_BLOCK 0x1000  /* v2: has directory block */

/* File types for v2 index entries */
#define MSYS_FILE_REG    0
#define MSYS_FILE_DIR    1
#define MSYS_FILE_SYMLINK 2
#define MSYS_FILE_CHR    3
#define MSYS_FILE_BLK    4
#define MSYS_FILE_FIFO   5
#define MSYS_FILE_SOCK   6

/* .msys file header (32 bytes on disk) */
struct msys_header {
	char     magic[8];       /* "Msys1\0\0\0" */
	uint64_t index_offset;   /* offset from file start to index block */
	uint32_t index_count;    /* number of entries in index */
	uint32_t flags;          /* flags */
	uint8_t  reserved[8];    /* reserved, must be zero */
};

/* v2 file header (64 bytes on disk) */
struct msys_header_v2 {
	char     magic[8];       /* "Msys2\0\0\0" */
	uint64_t index_offset;   /* offset to index block */
	uint32_t index_count;    /* number of entries */
	uint32_t flags;          /* flags */
	uint64_t dir_offset;     /* offset to directory block, 0 if none */
	uint32_t dir_count;      /* number of directory entries, 0 if none */
	uint32_t extension_offset; /* offset to first extension block, 0 if none */
	uint64_t data_size_total;  /* sum of all uncompressed data sizes */
	uint64_t content_hash;     /* first 8 bytes of SHA-256 of index block */
	uint8_t  reserved[16];     /* reserved, must be zero */
};

/* Index entry (16 + name_len bytes on disk, variable-length name tail) */
struct msys_index_entry {
	uint8_t name_hash[4];    /* FNV-1a 32-bit, little-endian */
	uint8_t data_offset[6];  /* uint48 little-endian */
	uint8_t data_size[4];    /* uint32 little-endian */
	uint8_t name_len[2];     /* uint16 little-endian */
	/* name follows (name_len bytes, no NUL terminator) */
};

/* v2 index entry (32 + name_len + optional 32 bytes SHA-256 on disk) */
struct msys_index_entry_v2 {
	uint8_t name_hash[4];       /* FNV-1a 32-bit, LE */
	uint8_t data_offset[6];     /* uint48 LE */
	uint8_t data_size[4];       /* uint32 LE: stored (possibly compressed) size */
	uint8_t uncompressed_size[4]; /* uint32 LE: original size, 0 if uncompressed */
	uint8_t file_type[2];       /* uint16 LE: MSYS_FILE_* */
	uint8_t mode[2];            /* uint16 LE: file permissions */
	uint8_t uid[4];             /* uint32 LE */
	uint8_t gid[4];             /* uint32 LE */
	uint8_t name_len;           /* uint8: name length (max 255) */
	uint8_t content_hash_present; /* 0 or 1 */
	/* name follows (name_len bytes, NUL NOT included) */
	/* content_hash follows name if content_hash_present==1 (32 bytes) */
};

/* Stat structure returned by msys_stat */
struct msys_stat {
	uint16_t  file_type;   /* MSYS_FILE_* */
	uint16_t  mode;        /* file permissions */
	uint32_t  uid;         /* owner uid */
	uint32_t  gid;         /* owner gid */
	uint64_t  size;        /* uncompressed data size */
	uint64_t  mtime;       /* modification time */
	uint32_t  name_hash;   /* FNV-1a hash of name */
};

_Static_assert(sizeof(struct msys_header) == 32, "msys_header must be 32 bytes");
_Static_assert(sizeof(struct msys_index_entry) == 16, "msys_index_entry must be 16 bytes");

/* In-memory handle for an open .msys file */
struct msys_chunk {
	void *ptr;
	struct msys_chunk *next;
};

struct msys {
	void   *base;            /* mmap base address */
	size_t  size;            /* file size */
	int     format_version;  /* MSYS_FORMAT_V1 or MSYS_FORMAT_V2 */
	union {
		struct msys_header   *hdr;   /* v1 header pointer */
		struct msys_header_v2 *hdr_v2; /* v2 header pointer */
	};
	struct msys_index_entry    *index;   /* v1 index (NULL for v2) */
	struct msys_index_entry_v2 *index_v2; /* v2 index (NULL for v1) */
	unsigned char **entries; /* private: per-entry pointers (variable len) */
	struct msys_chunk *chunks; /* private: allocated buffers (freed on close) */
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

/* Return the number of entries in the archive.
 *   m: handle from msys_open
 *   Returns entry count (0 if m is NULL). */
uint32_t msys_count(struct msys *m);

/* Enumerate entry at position idx (0-based).
 *   m:    handle from msys_open
 *   idx:  entry index (0 ... msys_count(m)-1)
 *   name: out parameter, receives pointer to name string (NOT NUL-terminated)
 *   nlen: out parameter, receives name length in bytes
 *   size: out parameter, receives data size in bytes
 *   Returns 0 on success, -1 on error (errno = ERANGE if idx out of range). */
int msys_enumerate(struct msys *m, uint32_t idx,
                   const char **name, size_t *nlen, size_t *size);

/* Callback for msys_readdir.
 *   name:  immediate child name (NOT NUL-terminated, single path component)
 *   nlen:  name length in bytes
 *   size:  data size in bytes (0 for synthetic directories)
 *   is_dir: non-zero if this child appears to be a subdirectory
 *   arg:   opaque user pointer passed to msys_readdir
 *   Returns 0 to continue iteration, non-zero to stop. */
typedef int (*msys_dir_cb)(const char *name, size_t nlen,
                           size_t size, int is_dir, void *arg);

/* List immediate children of a directory within the archive.
 *   m:   handle from msys_open
 *   dir: directory path, e.g. "usr/lib" or "" for root
 *   cb:  callback invoked for each immediate child
 *   arg: opaque pointer passed through to cb
 *   Returns 0 on success, -1 on error (errno set).
 *   If dir is not found or has no children, returns 0 (no cb calls).
 *   This is a linear scan (O(N) over total entries). */
int msys_readdir(struct msys *m, const char *dir, msys_dir_cb cb, void *arg);

/* Stat a file by name — check existence and get data size.
 *   m:    handle from msys_open
 *   name: NUL-terminated path
 *   size: out parameter, receives data size (may be NULL)
 *   Returns 0 on success, -1 on error (errno set: ENOENT, ENOMSG). */
int msys_fstat(struct msys *m, const char *name, size_t *size);

/* Get format version of opened .msys.
 *   Returns MSYS_FORMAT_V1 or MSYS_FORMAT_V2 (0 if m is NULL). */
int msys_format_version(struct msys *m);

/* Get metadata for a path within the archive.
 *   m:    handle from msys_open
 *   name: NUL-terminated path
 *   st:   out parameter, receives metadata (may be NULL for existence check)
 *   Returns 0 on success, -1 on error (errno set).
 *   For v1 archives, file_type = MSYS_FILE_REG, other fields are zeroed.
 *   For v2 archives, returns the stored metadata. */
int msys_stat(struct msys *m, const char *name, struct msys_stat *st);

/* Read symlink target from the archive.
 *   m:       handle from msys_open
 *   name:    NUL-terminated path
 *   buf:     destination buffer
 *   bufsize: buffer size
 *   Returns number of bytes written (not including NUL) on success, -1 on error
 *   (errno = EINVAL if not a symlink, ENOENT if not found). */
int msys_readlink(struct msys *m, const char *name, char *buf, size_t bufsize);

/* FNV-1a 32-bit hash helper.
 *   name: pointer to data to hash
 *   len:  number of bytes
 *   Returns 32-bit FNV-1a hash. */
uint32_t msys_fnv1a(const unsigned char *name, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* MT_MSYS_H */
