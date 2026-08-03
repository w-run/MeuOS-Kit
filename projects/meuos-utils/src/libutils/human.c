/* libutils/human.c — 人类可读字节数格式化 */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "meuos/utils.h"

char *human_readable(uint64_t bytes, int si) {
    /* si=1: 1KB = 1000B (KB); si=0: 1KiB = 1024B (K) */
    char buf[16];
    double v = (double)bytes;
    const char *units = si ? "kMGTPE" : "KMGTPE";
    int i = 0;
    while (v >= 1024.0 && i < 5) {
        v /= si ? 1000.0 : 1024.0;
        i++;
    }
    /* GNU df/ls 默认保留 1 位小数；0.x 整数化 */
    if (i == 0) {
        snprintf(buf, sizeof(buf), si ? "%" PRIu64 "B" : "%" PRIu64,
                 bytes);
    } else {
        char unit = units[i - 1];
        snprintf(buf, sizeof(buf), "%.1f%c", v, si ? unit : (char)(unit + ('a' - 'A')));
    }
    return xstrdup(buf);
}
