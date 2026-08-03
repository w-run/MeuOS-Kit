/* hex.c — 十六进制字符串转换
 *
 * 为 md5sum/sha256sum 等工具提供统一的二进制 ↔ 十六进制转换。
 * 消除 md5_hex/sha256_hex 的重复逻辑。
 */
#define _POSIX_C_SOURCE 200809L

#include <string.h>
#include <ctype.h>

#include "meuos/utils.h"

static const char hex_digits[] = "0123456789abcdef";

void bytes_to_hex(const unsigned char *data, size_t len, char *out) {
    for (size_t i = 0; i < len; i++) {
        out[i * 2]     = hex_digits[data[i] >> 4];
        out[i * 2 + 1] = hex_digits[data[i] & 0xf];
    }
    out[len * 2] = '\0';
}

int hex_to_bytes(const char *hex, unsigned char *out, size_t max_out) {
    size_t written = 0;
    while (*hex && written < max_out) {
        /* 跳过空格和分隔符 */
        while (*hex && (isspace((unsigned char)*hex) || *hex == ':' || *hex == '-'))
            hex++;
        if (!*hex) break;

        /* 解析高四位 */
        int hi;
        if (*hex >= '0' && *hex <= '9') hi = *hex - '0';
        else if (*hex >= 'a' && *hex <= 'f') hi = *hex - 'a' + 10;
        else if (*hex >= 'A' && *hex <= 'F') hi = *hex - 'A' + 10;
        else return -1;
        hex++;

        /* 解析低四位 */
        if (!*hex) return -1;
        int lo;
        if (*hex >= '0' && *hex <= '9') lo = *hex - '0';
        else if (*hex >= 'a' && *hex <= 'f') lo = *hex - 'a' + 10;
        else if (*hex >= 'A' && *hex <= 'F') lo = *hex - 'A' + 10;
        else return -1;
        hex++;

        out[written++] = (unsigned char)((hi << 4) | lo);
    }
    return (int)written;
}
