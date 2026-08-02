/* uname — 显示系统信息
 * 用法：uname [OPTION]...
 * 选项：-a 全部, -s 内核名, -n 节点名, -r 内核发行, -v 版本, -m 机器硬件, -p 处理器
 */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "meuos/utils.h"

int main(int argc, char **argv) {
    int argi = utils_init(argc, argv);
    if (argi < argc && !strcmp(argv[argi], "--help")) utils_usage("Usage: uname [-a] [-s] [-n] [-r] [-v] [-m] [-p]\n");
    struct utsname u;
    if (uname(&u) < 0) { perror("uname"); return 1; }
    int all = 0, want_s = 0, want_n = 0, want_r = 0, want_v = 0, want_m = 0, want_p = 0;
    while (argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0') {
        for (char *p = argv[argi]+1; *p; p++) {
            switch (*p) {
            case 'a': all = 1; break;
            case 's': want_s = 1; break;
            case 'n': want_n = 1; break;
            case 'r': want_r = 1; break;
            case 'v': want_v = 1; break;
            case 'm': want_m = 1; break;
            case 'p': want_p = 1; break;
            default: fprintf(stderr, "uname: unknown option -%c\n", *p); return 2;
            }
        }
        argi++;
    }
    if (all) want_s = want_n = want_r = want_v = want_m = want_p = 1;
    if (!want_s && !want_n && !want_r && !want_v && !want_m && !want_p) want_s = 1;

    int first = 1;
    if (want_s) { printf("%s%s", first?"":" ", u.sysname); first = 0; }
    if (want_n) { printf("%s%s", first?"":" ", u.nodename); first = 0; }
    if (want_r) { printf("%s%s", first?"":" ", u.release); first = 0; }
    if (want_v) { printf("%s%s", first?"":" ", u.version); first = 0; }
    if (want_m) { printf("%s%s", first?"":" ", u.machine); first = 0; }
    if (want_p) { printf("%s%s", first?"":" ", u.machine); first = 0; }  /* processor 简化 */
    putchar('\n');
    return 0;
}
