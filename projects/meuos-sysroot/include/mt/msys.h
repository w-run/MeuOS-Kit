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
#define MSYS_F_MZ        0x08    /* meuos-compress (libmz.a) codec */
#define MSYS_F_INCREMENTAL 0x04  /* incremental mode */
#define MSYS_F_DEDUP     0x0100  /* v2: content dedup */
#define MSYS_F_SIGNED    0x0200  /* v2: has signature extension */
#define MSYS_F_DIR_BLOCK 0x1000  /* v2: has directory block */
#define MSYS_F_STREAMING 0x2000  /* v2: sequential streaming layout */

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

/* Register the meuos-compress (libmz.a) decompressor for MSYS_F_MZ data.
 * Optional: consumers that link libmz.a call this to enable mz-decompressed
 * archives; libmsys itself has no link-time dependency on the codec. */
void msys_set_mz_codec(int (*fn)(const void *in, size_t il, void **r, size_t *rl));

/* Get a human-readable error description for the last libmsys error.
 * This returns a thread-local string describing what went wrong, useful
 * for producing friendly error messages in CLI tools.
 *   err: the errno value from the failed call (may be used for categorization)
 *   Returns a pointer to a thread-local string (valid until next libmsys call). */
const char *msys_strerror(int err);

/* Open a .msys file for reading.
 *   path: path to .msys file
 *   Returns a handle, or NULL on error (errno set, msys_strerror has details). */
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

/* Get extended attribute for a file in the archive.
 * xattr entries are stored as @xattr/<name> with format: key=value\n...
 *   m:       handle from msys_open
 *   name:    NUL-terminated path
 *   key:     xattr key (e.g. "user.mime_type")
 *   buf:     destination buffer for value
 *   bufsize: buffer size
 *   Returns length of value on success, -1 on error (errno set). */
int msys_getxattr(struct msys *m, const char *name, const char *key,
                  char *buf, size_t bufsize);

/* Verify content integrity of a single file by recomputing SHA-256.
 *   m:    handle from msys_open
 *   name: NUL-terminated path
 *   Returns 0 on success (hash matches), -1 on error or mismatch (errno set).
 *   For v1 files or entries without SHA-256, returns 0 (cannot verify). */
int msys_verify(struct msys *m, const char *name);

/* Verify all files in the archive.
 *   m: handle from msys_open
 *   Returns 0 if all verifiable entries match, -1 on first mismatch. */
int msys_verify_all(struct msys *m);

/* Get the first extension block with the given type (v2 only).
 * Extension blocks are stored after the index in the format:
 *   type[4] (uint32 LE) | length[4] (uint32 LE) | data[length]
 *   m:     handle from msys_open
 *   type:  type identifier (e.g., fourcc code like 0x6e676973 = "sign")
 *   data:  out parameter, receives pointer to data within mmap'd region
 *   dlen:  out parameter, receives data length in bytes
 *   Returns 0 on success, -1 if type not found (errno = ENOENT)
 *   or archive is v1/not a v2 archive (errno = ENOSYS). */
int msys_get_extension(struct msys *m, uint32_t type,
                       const void **data, uint32_t *dlen);

/* Verify the ed25519 signature stored in extension block (v2 only).
 * The signature is computed over the entire index block.
 *   m:   handle from msys_open
 *   pk:  32-byte ed25519 public key
 *   Returns 0 if signature is valid, -1 if invalid or no signature
 *   (errno = ENOENT if no signature, EINVAL if signature bytes wrong length,
 *    ENOSYS for v1 archives, ENOPKG if libsodium unavailable. */
int msys_verify_signature(struct msys *m, const uint8_t pk[32]);

/* FNV-1a 32-bit hash helper.
 *   name: pointer to data to hash
 *   len:  number of bytes
 *   Returns 32-bit FNV-1a hash. */
uint32_t msys_fnv1a(const unsigned char *name, size_t len);

/* ---- Overlay / layering API ---- */

/* Overlay handle: stack of .msys files.
 * Layer 0 = base (lowest priority), last layer added = top (highest priority).
 * Search checks from top to bottom; first match wins. */
struct msys_overlay {
	struct msys **layers;
	int           count;
	int           cap;
};

/* Open multiple .msys files as layers.
 *   paths: array of paths, paths[0] = base, paths[count-1] = top.
 *   count: number of paths.
 *   Returns handle, or NULL on error (all opened handles closed on failure). */
struct msys_overlay *msys_overlay_open(const char **paths, int count);

/* Add a layer on top (highest priority).
 *   ol:   overlay handle.
 *   path: path to .msys file to add.
 *   Returns 0 on success, -1 on error. */
int msys_overlay_add(struct msys_overlay *ol, const char *path);

/* Number of layers. */
int msys_overlay_count(struct msys_overlay *ol);

/* Get layer handle by index (0 = base). */
struct msys *msys_overlay_get(struct msys_overlay *ol, int idx);

/* Search across layers, top (highest priority) first.
 *   Returns data pointer from the first layer containing name.
 *   *layer receives the layer index that matched (may be NULL). */
const void *msys_overlay_search(struct msys_overlay *ol, const char *name,
                                size_t *size, int *layer);

/* Read a file from the first layer that has it. */
int msys_overlay_read(struct msys_overlay *ol, const char *name,
                      void *buf, size_t buflen);

/* VFS: fopen from first layer that has the path. */
FILE *msys_overlay_fopen(struct msys_overlay *ol, const char *path,
                         const char *mode);

/* Load file content from first layer that has it. */
int msys_overlay_load(struct msys_overlay *ol, const char *path,
                      void **buf, size_t *size);

/* Stat a file across layers (first match wins). */
int msys_overlay_stat(struct msys_overlay *ol, const char *name,
                      struct msys_stat *st);

/* Readlink from first layer that has the path. */
int msys_overlay_readlink(struct msys_overlay *ol, const char *name,
                          char *buf, size_t bufsize);

/* Read directory listing merged from all layers (dedup by child name).
 *   For each immediate child of dir, cb is called at most once.
 *   A file in a higher layer shadows (replaces) the same file in a lower
 *   layer in the listing — only the topmost occurrence is reported. */
int msys_overlay_readdir(struct msys_overlay *ol, const char *dir,
                         msys_dir_cb cb, void *arg);

/* Verify a file's SHA-256 in the first layer that has it. */
int msys_overlay_verify(struct msys_overlay *ol, const char *name);

/* Close all layers and free the overlay handle.
 *   ol: handle to close (NULL is safe). */
void msys_overlay_close(struct msys_overlay *ol);

/* ---- Streaming API ---- */

/* Streaming reader for .msys archives with MSYS_F_STREAMING layout.
 * Allows sequential read of all (name, data) pairs without seeking. */
struct msys_stream;

/* Open a .msys file in streaming mode.
 *   path:  path to .msys file
 *   Returns a stream handle, or NULL on error (errno set). */
struct msys_stream *msys_stream_open(const char *path);

/* Read the next entry from the stream.
 *   s:     stream handle from msys_stream_open
 *   name:  out parameter, receives pointer to entry name (NOT NUL-terminated)
 *   nlen:  out parameter, receives name length
 *   data:  out parameter, receives pointer to entry data
 *   dsize: out parameter, receives data size in bytes
 *   Returns 1 on success, 0 at end of stream, -1 on error (errno set). */
int msys_stream_next(struct msys_stream *s,
                     const char **name, size_t *nlen,
                     const void **data, size_t *dsize);

/* Close a streaming reader. */
void msys_stream_close(struct msys_stream *s);

#ifdef __cplusplus
}
#endif

#endif /* MT_MSYS_H */
