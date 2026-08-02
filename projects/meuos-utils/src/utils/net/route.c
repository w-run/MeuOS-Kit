/* route — 路由表查看器
 *
 * 用法：route [-n] [-e]
 *
 * 读取 /proc/net/route 显示内核路由表。
 * -n: 不解析主机名（数字地址）
 * -e: 显示扩展信息
 *
 * 共享代码通过 netinfo 模块复用（与 ip/ifconfig/netstat 共用）。
 *
 * --classic: 传统 route 格式
 */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "meuos/utils.h"
#include "meuos/netinfo.h"

static void usage(void) {
    fprintf(stderr,
        "Usage: route [-n] [-e]\n"
        "  -n    Don't resolve names (numeric output)\n"
        "  -e    Show extended info\n"
        "  --classic  Traditional format\n");
}

static char *flags_str(unsigned int flags) {
    static char buf[64];
    buf[0] = '\0';
    if (flags & 0x0001) strcat(buf, "U");
    if (flags & 0x0002) strcat(buf, "G");
    if (flags & 0x0004) strcat(buf, "H");
    if (flags & 0x0008) strcat(buf, "R");
    if (flags & 0x0010) strcat(buf, "D");
    if (flags & 0x0020) strcat(buf, "M");
    if (buf[0] == '\0') strcat(buf, "-");
    return buf;
}

int main(int argc, char **argv) {
    set_program_name(argv[0]);
    utils_classic_init(argc, argv);

    int numeric = 0, extended = 0;
    int argi = 1;
    while (argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0') {
        if (!strcmp(argv[argi], "--version")) { version(); }
        if (!strcmp(argv[argi], "--help")) { usage(); return 0; }
        if (!strcmp(argv[argi], "--classic")) { argi++; continue; }
        for (char *p = argv[argi] + 1; *p; p++) {
            if (*p == 'n') numeric = 1;
            else if (*p == 'e') extended = 1;
            else { fprintf(stderr, "route: unknown option -%c\n", *p); return 2; }
        }
        argi++;
    }

    (void)numeric; /* -n: numeric output (always numeric in current impl) */
    FILE *f = fopen("/proc/net/route", "r");
    if (!f) {
        perror("route: cannot open /proc/net/route");
        return 1;
    }

    /* 打印表头 */
    if (extended) {
        printf("Kernel IP routing table\n");
        printf("%-16s %-16s %-16s %-6s %-6s %-6s %-6s %s\n",
               "Destination", "Gateway", "Genmask", "Flags",
               "Metric", "Ref", "Use", "Iface");
    } else {
        printf("Kernel IP routing table\n");
        printf("%-16s %-16s %-16s %-6s %-6s %s\n",
               "Destination", "Gateway", "Genmask", "Flags",
               "Metric", "Iface");
    }

    struct route_entry e;
    while (netinfo_route_read_line(f, &e) == 0) {
        if (extended) {
            printf("%-16s %-16s %-16s %-6s %-6d %-6d %-6d %s\n",
                    fmt_hex_ip(e.dest), fmt_hex_ip(e.gateway), fmt_hex_ip(e.mask),
                    flags_str(e.flags), e.metric, e.ref, e.use, e.iface);
        } else {
            printf("%-16s %-16s %-16s %-6s %-6d %s\n",
                    fmt_hex_ip(e.dest), fmt_hex_ip(e.gateway), fmt_hex_ip(e.mask),
                    flags_str(e.flags), e.metric, e.iface);
        }
    }
    fclose(f);
    return 0;
}
