/* mz_main.c — MeuOS MZ v2 主入口模块
 * 统一调度 compress/decompress 到各 codec 实现
 */
#include "mz.h"

int
mz_compress(const void *in, size_t il, void **r, size_t *rl, int c, int lv)
{
    switch (c) {
    case MZ_CODEC_LZ77:
        return mz_compress_lz77(in, il, r, rl, lv);
    default:
        return MZ_ERR_CODEC;
    }
}

int
mz_decompress(const void *in, size_t il, void **r, size_t *rl, int c)
{
    switch (c) {
    case MZ_CODEC_LZ77:
        return mz_decompress_lz77(in, il, r, rl);
    default:
        return MZ_ERR_CODEC;
    }
}

size_t
mz_max_compressed_size(size_t il, int c)
{
    (void)c;
    return mz_max_compressed_size_lz77(il);
}

const char *
mz_strerror(int e)
{
    switch (e) {
    case MZ_OK:         return "Success";
    case MZ_ERR_MEMORY: return "Out of memory";
    case MZ_ERR_DATA:   return "Corrupt or invalid data";
    case MZ_ERR_PARAM:  return "Invalid parameter";
    case MZ_ERR_STREAM: return "Stream error";
    case MZ_ERR_CODEC:  return "Unsupported codec";
    case MZ_ERR_CRYPT:  return "Cryptographic error";
    default:            return "Unknown error";
    }
}

int
mz2_level_supported(int level)
{
    return (level >= 1 && level <= 9) ? 1 : 0;
}
