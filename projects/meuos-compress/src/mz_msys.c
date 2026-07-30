/* mz_msys.c — mz codec adapter for .msys (dlopen-compatible flat API)
 *
 * Exposes mz_compress/mz_decompress under simple C-linkage symbols
 * that msys can look up via dlsym(handle, "mz_msys_compress") etc.
 *
 * API convention (zlib-style flat buffers):
 *   User provides output buffer + capacity via *outlen.
 *   On success 0 is returned and *outlen set to actual size.
 *   On error -1 is returned; content of output / *outlen is undefined.
 */

#include "mz.h"
#include <stdlib.h>
#include <string.h>

int
mz_msys_compress(const unsigned char *in, size_t inlen,
                 unsigned char *out, size_t *outlen, int level)
{
    void *tmp = NULL;
    size_t tmplen = 0;
    int ret;

    ret = mz_compress(in, inlen, &tmp, &tmplen,
                      MZ_CODEC_MEUOS, level);
    if (ret != MZ_OK)
        return -1;

    if (tmplen > *outlen) {
        free(tmp);
        return -1;
    }

    memcpy(out, tmp, tmplen);
    *outlen = tmplen;
    free(tmp);
    return 0;
}

int
mz_msys_decompress(const unsigned char *in, size_t inlen,
                   unsigned char *out, size_t *outlen)
{
    void *tmp = NULL;
    size_t tmplen = 0;
    int ret;

    ret = mz_decompress(in, inlen, &tmp, &tmplen,
                        MZ_CODEC_MEUOS);
    if (ret != MZ_OK)
        return -1;

    if (tmplen > *outlen) {
        free(tmp);
        return -1;
    }

    memcpy(out, tmp, tmplen);
    *outlen = tmplen;
    free(tmp);
    return 0;
}
