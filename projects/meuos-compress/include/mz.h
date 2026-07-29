#ifndef MZ_H
#define MZ_H
#include <stddef.h>
#include <stdint.h>
#define MZ_CODEC_LZ77 1
#define MZ_OK 0
#define MZ_ERR_MEMORY -1
#define MZ_ERR_DATA -2
#define MZ_ERR_PARAM -3
#define MZ_ERR_STREAM -4
#define MZ_ERR_CODEC -5
int mz_compress(const void *in, size_t il, void **r, size_t *rl, int c, int lv);
int mz_decompress(const void *in, size_t il, void **r, size_t *rl, int c);
size_t mz_max_compressed_size(size_t il, int c);
const char *mz_strerror(int e);
#endif
