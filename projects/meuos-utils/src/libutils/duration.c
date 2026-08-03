/* duration.c — 时长字符串解析
 *
 * 为 sleep/timeout 等工具提供统一的时长解析功能。
 * 支持以下格式：
 *   简单后缀: "5", "5s", "5m", "5h", "5d"
 *   复合时长: "1h30m", "2h15m30s"（每个后缀可单独或组合使用）
 *   时间格式: "1:30" (MM:SS), "1:30:00" (HH:MM:SS)
 *
 * 增强：相比原始手写版本，新增复合时长和冒号时间格式支持。
 */
#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "meuos/utils.h"

/* 解析单个时长单元：数字 + 可选后缀(s/m/h/d)
 * 推进 *p 到下一个单元的开始位置
 * 返回该单元的秒数，失败返回 -1.0 */
static double parse_unit(const char **p) {
    char *end;
    double val = strtod(*p, &end);
    if (end == *p)
        return -1.0; /* 不是数字 */

    double mult = 1.0;
    switch (*end) {
    case '\0': /* 纯数字 → 秒 */ break;
    case 's':  mult = 1.0;      end++; break;
    case 'm':  mult = 60.0;     end++; break;
    case 'h':  mult = 3600.0;   end++; break;
    case 'd':  mult = 86400.0;  end++; break;
    default:
        /* 检查是否是冒号时间格式（交由调用方处理） */
        if (*end == ':') return val; /* 返回原始值，不推进 */
        return -1.0; /* 未知后缀 */
    }
    *p = end;
    return val * mult;
}

double parse_duration(const char *s) {
    if (!s || !*s)
        return -1.0;

    /* 冒号时间格式: MM:SS 或 HH:MM:SS */
    if (strchr(s, ':')) {
        int h = 0, m, sec;
        int n = sscanf(s, "%d:%d:%d", &h, &m, &sec);
        if (n == 2) {
            /* MM:SS 格式 */
            return (double)(h * 60 + m);
        }
        if (n == 3) {
            /* HH:MM:SS 格式 */
            return (double)(h * 3600 + m * 60 + sec);
        }
        return -1.0;
    }

    /* 复合时长格式: 1h30m20s 等 */
    const char *p = s;
    double total = 0;
    while (*p) {
        double unit = parse_unit(&p);
        if (unit < 0)
            return -1.0;
        total += unit;
    }
    return total;
}

int parse_duration_ts(const char *s, struct timespec *ts) {
    double d = parse_duration(s);
    if (d < 0)
        return -1;
    ts->tv_sec = (time_t)d;
    ts->tv_nsec = (long)((d - (double)ts->tv_sec) * 1000000000L);
    return 0;
}
