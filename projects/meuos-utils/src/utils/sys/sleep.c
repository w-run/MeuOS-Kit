/* sleep — 暂停执行指定秒数
 * 用法：sleep NUMBER[SUFFIX]...
 * SUFFIX: s (秒), m (分), h (时), d (天)
 * 支持: 复合时长 (1h30m), 冒号格式 (1:30:00)
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "meuos/utils.h"

int main(int argc, char **argv) {
    int argi = utils_init(argc, argv);
    if (argi < argc && !strcmp(argv[argi], "--help")) utils_usage("Usage: sleep NUMBER[SUFFIX]...\n");
    if (argi >= argc) { fprintf(stderr, "sleep: missing operand\n"); return 2; }
    struct timespec total = {0, 0};
    for (int i = argi; i < argc; i++) {
        struct timespec ts;
        if (parse_duration_ts(argv[i], &ts) < 0) {
            fprintf(stderr, "sleep: invalid time interval '%s'\n", argv[i]);
            return 2;
        }
        total.tv_sec += ts.tv_sec;
        total.tv_nsec += ts.tv_nsec;
        if (total.tv_nsec >= 1000000000L) { total.tv_sec++; total.tv_nsec -= 1000000000L; }
    }
    if (total.tv_sec <= 0 && total.tv_nsec <= 0) return 0;
    nanosleep(&total, NULL);
    return 0;
}
