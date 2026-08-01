/* mz_hash.c — CRC-32 / Adler-32 校验和
 *
 * CRC-32: IEEE 802.3 多项式 (0xEDB88320 reversed),
 *         与 zlib/gzip/PNG 使用的 CRC-32 完全一致。
 *
 * Adler-32: RFC 1950 定义的 Adler-32,
 *           与 zlib deflate 流头校验使用的算法一致。
 *
 * 两者都提供增量式 API（init → update → final）和一次性 API。
 * 纯 C11 实现，零外部依赖。
 */

#include "mz.h"
#include <stdint.h>
#include <string.h>

/* ===================================================================
 * CRC-32 (IEEE 802.3, poly 0xEDB88320)
 * =================================================================== */

/* 运行时生成 CRC-32 查找表，避免硬编码表占用代码段。
 * 使用 0xEDB88320 (反射多项式) 生成。
 */
static uint32_t crc32_table[256];
static int crc32_table_ready = 0;

static void
crc32_init_table(void)
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320u;
            else
                crc >>= 1;
        }
        crc32_table[i] = crc;
    }
    crc32_table_ready = 1;
}

/* 一次性 CRC-32 计算 */
uint32_t
mz_crc32(const void *data, size_t len)
{
    if (!crc32_table_ready) crc32_init_table();

    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++)
        crc = crc32_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

/* 增量式 CRC-32 — init */
uint32_t
mz_crc32_init(void)
{
    return 0xFFFFFFFFu;
}

/* 增量式 CRC-32 — update，返回当前状态 */
uint32_t
mz_crc32_update(uint32_t crc, const void *data, size_t len)
{
    if (!crc32_table_ready) crc32_init_table();

    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < len; i++)
        crc = crc32_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

/* 增量式 CRC-32 — final */
uint32_t
mz_crc32_final(uint32_t crc)
{
    return crc ^ 0xFFFFFFFFu;
}

/* ===================================================================
 * Adler-32 (RFC 1950)
 * ===================================================================
 *
 * s1 = (s1 + data[i]) mod 65521
 * s2 = (s2 + s1)       mod 65521
 * adler32 = (s2 << 16) | s1
 *
 * 优化：延迟取模，只在 s2 可能溢出前统一处理。
 * 每次迭代累加 s1 += data[i], s2 += s1。
 * s1 最大 65520 + 255 = 65775，s2 最大 65520 + 65775 = 131295。
 * 均在 32-bit 范围内安全，可累积最多 NMAX 次后一次性取模。
 */

#define MZ_ADLER_BASE 65521u
#define MZ_ADLER_NMAX 5552u  /* 最大可安全累积的次数 */

uint32_t
mz_adler32(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t s1 = 1, s2 = 0;

    while (len > 0) {
        size_t block = len > MZ_ADLER_NMAX ? MZ_ADLER_NMAX : len;
        len -= block;
        for (size_t i = 0; i < block; i++) {
            s1 += p[i];
            s2 += s1;
        }
        s1 %= MZ_ADLER_BASE;
        s2 %= MZ_ADLER_BASE;
        p += block;
    }
    return (s2 << 16) | s1;
}

/* 增量式 Adler-32 — init */
uint32_t
mz_adler32_init(void)
{
    return 1;  /* s1=1, s2=0 → (0 << 16) | 1 = 1 */
}

/* 增量式 Adler-32 — update */
uint32_t
mz_adler32_update(uint32_t adler, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t s1 = adler & 0xFFFF;
    uint32_t s2 = (adler >> 16) & 0xFFFF;

    while (len > 0) {
        size_t block = len > MZ_ADLER_NMAX ? MZ_ADLER_NMAX : len;
        len -= block;
        for (size_t i = 0; i < block; i++) {
            s1 += p[i];
            s2 += s1;
        }
        s1 %= MZ_ADLER_BASE;
        s2 %= MZ_ADLER_BASE;
        p += block;
    }
    return (s2 << 16) | s1;
}

/* 增量式 Adler-32 — final (identity, 返回当前状态) */
uint32_t
mz_adler32_final(uint32_t adler)
{
    return adler;
}
