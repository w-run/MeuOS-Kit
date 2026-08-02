/* route — 路由表查看器
 *
 * 用法：route [-n] [-e]
 *
 * 读取 /proc/net/route 显示内核路由表。
 * -n: 不解析主机名（数字地址）
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

static void usage(void) {
    fprintf(stderr,
        "Usage: route [-n] [-e]\n"
        "  -n    Don't resolve names (numeric output)\n"
        "  -e    Show extended info\n"
        "  --classic  Traditional format\n");
}

/* 将十六进制内核路由地址转为点分十进制 */
static char *hex_to_ip(unsigned int hex) {
    static char buf[32];
    struct in_addr addr;
    addr.s_addr = hex;  /* 内核存储为小端，与 in_addr 一致 */
    inet_ntop(AF_INET, &addr, buf, sizeof(buf));
    return buf;
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

    FILE *f = fopen("/proc/net/route", "r");
    if (!f) {
        perror("route: cannot open /proc/net/route");
        return 1;
    }

    char line[512];
    /* 打印表头 */
    printf("Kernel IP routing table\n");
    printf("%-16s %-16s %-16s %-6s %-6s %-6s %s\n",
           "Destination", "Gateway", "Genmask", "Flags",
           "Metric", "Ref", "Iface");
    /* 跳过标题行 */
    fgets(line, sizeof(line), f);
    while (fgets(line, sizeof(line), f)) {
        char iface[16];
        unsigned int dest, gw, mask;
        unsigned int flags, metric, ref, use;
        if (sscanf(line, "%15s %x %x %x %d %d %d %d",
                   iface, &dest, &gw, &mask, &flags, &metric, &ref, &use) < 8)
            continue;
        printf("%-16s %-16s %-16s %-6s %-6d %-6d %s\n",
                hex_to_ip(dest), hex_to_ip(gw), hex_to_ip(mask),
                flags_str(flags), metric, ref, iface);
    }
    fclose(f);
    return 0;
}
