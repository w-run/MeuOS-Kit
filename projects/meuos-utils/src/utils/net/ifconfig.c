/* ifconfig — 网络接口配置查看器
 *
 * 用法：ifconfig [interface]
 *
 * 读取 /proc/net/dev 和 /sys/class/net/ 获取接口信息。
 * 支持 -a 显示全部（含未启用接口）、-s 精简统计模式。
 * 共享代码通过 netinfo 模块复用（与 ip/route/netstat 共用）。
 *
 * --classic: 传统 ifconfig 格式
 */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <ifaddrs.h>
#include <netdb.h>

#include "meuos/utils.h"
#include "meuos/netinfo.h"

static void usage(void) {
    fprintf(stderr,
        "Usage: ifconfig [-a] [-s] [interface]\n"
        "  -a    Show all interfaces (including inactive)\n"
        "  -s    Short list output\n"
        "  --classic  Traditional format\n");
}

static void print_human_bytes(uint64_t b) {
    if (b < 1024) printf("%llu", (unsigned long long)b);
    else if (b < 1024*1024) printf("%.1fK", (double)b / 1024);
    else if (b < 1024*1024*1024) printf("%.1fM", (double)b / (1024*1024));
    else printf("%.1fG", (double)b / (1024*1024*1024));
}

static int show_interface(const char *ifname, int short_mode) {
    if (short_mode) {
        /* 精简模式：类似 ifconfig -s */
        FILE *f = fopen("/proc/net/dev", "r");
        if (!f) return 1;
        struct net_dev_stats st;
        while (netinfo_dev_read_line(f, &st) == 0) {
            if (ifname && strcmp(st.name, ifname) != 0) continue;
            printf("%-10s ", st.name);
            print_human_bytes(st.rx_bytes);
            printf("  ");
            print_human_bytes(st.tx_bytes);
            printf("\n");
        }
        fclose(f);
        return 0;
    }

    /* 详细模式：使用 getifaddrs 获取地址信息 */
    struct ifaddrs *ifa, *ifa0;
    if (getifaddrs(&ifa0) != 0) {
        perror("ifconfig: getifaddrs");
        return 1;
    }

    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) {
        perror("ifconfig: socket");
        freeifaddrs(ifa0);
        return 1;
    }

    const char *cur_ifname = ifname;
    int showed = 0;
    for (ifa = ifa0; ifa; ifa = ifa->ifa_next) {
        if (cur_ifname && strcmp(ifa->ifa_name, cur_ifname) != 0)
            continue;
        if (!ifa->ifa_addr)
            continue;

        /* 只在第一次遇到这个接口时打印头 */
        if (!showed || strcmp(ifa->ifa_name, ifname ? ifname : "") != 0) {
            showed = 1;
            cur_ifname = ifa->ifa_name;

            /* 通过共享 ioctl 封装获取 flags/MTU */
            struct if_info info;
            if (ifinfo_get(s, ifa->ifa_name, &info) == 0) {
                printf("\n%s: flags=%d  mtu %d\n",
                       ifa->ifa_name, info.flags, info.mtu);
            } else {
                printf("\n%s: flags=%d\n", ifa->ifa_name, ifa->ifa_flags);
            }
        }

        char host[NI_MAXHOST];
        socklen_t salen = (ifa->ifa_addr->sa_family == AF_INET)
            ? sizeof(struct sockaddr_in)
            : sizeof(struct sockaddr_in6);
        if (getnameinfo(ifa->ifa_addr, salen, host, sizeof(host),
                        NULL, 0, NI_NUMERICHOST) == 0) {
            const char *fam = (ifa->ifa_addr->sa_family == AF_INET)
                ? "inet" : "inet6";
            printf("    %s %s", fam, host);
            if (ifa->ifa_netmask) {
                char mask[NI_MAXHOST];
                if (getnameinfo(ifa->ifa_netmask, salen, mask, sizeof(mask),
                                NULL, 0, NI_NUMERICHOST) == 0)
                    printf("  netmask %s", mask);
            }
            if (ifa->ifa_flags & IFF_BROADCAST && ifa->ifa_broadaddr) {
                char bcast[NI_MAXHOST];
                if (getnameinfo(ifa->ifa_broadaddr, salen, bcast, sizeof(bcast),
                                NULL, 0, NI_NUMERICHOST) == 0)
                    printf("  broadcast %s", bcast);
            }
            printf("\n");
        }
    }

    /* 获取 MAC 地址 */
    if (cur_ifname) {
        struct if_info info;
        if (ifinfo_get(s, cur_ifname, &info) == 0 && info.has_mac) {
            printf("    ether %s\n", fmt_mac(info.mac));
        }
    }
    close(s);

    /* 统计信息 */
    FILE *f = fopen("/proc/net/dev", "r");
    if (f) {
        struct net_dev_stats st;
        while (netinfo_dev_read_line(f, &st) == 0) {
            if (cur_ifname && strcmp(st.name, cur_ifname) != 0) continue;
            printf("    RX packets %lu  bytes %lu", st.rx_packets, st.rx_bytes);
            if (st.rx_errors) printf("  errors %lu", st.rx_errors);
            if (st.rx_dropped) printf("  dropped %lu", st.rx_dropped);
            printf("\n");
            printf("    TX packets %lu  bytes %lu", st.tx_packets, st.tx_bytes);
            if (st.tx_errors) printf("  errors %lu", st.tx_errors);
            if (st.tx_dropped) printf("  dropped %lu", st.tx_dropped);
            if (st.tx_collisions) printf("  collisions %lu", st.tx_collisions);
            printf("\n");
            break;
        }
        fclose(f);
    }

    freeifaddrs(ifa0);
    return 0;
}

int main(int argc, char **argv) {
    set_program_name(argv[0]);
    utils_classic_init(argc, argv);

    int all = 0, short_mode = 0;
    int argi = 1;
    while (argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0') {
        if (!strcmp(argv[argi], "--version")) { version(); }
        if (!strcmp(argv[argi], "--help")) { usage(); return 0; }
        if (!strcmp(argv[argi], "--classic")) { argi++; continue; }
        for (char *p = argv[argi] + 1; *p; p++) {
            if (*p == 'a') all = 1;
            else if (*p == 's') short_mode = 1;
            else { fprintf(stderr, "ifconfig: unknown option -%c\n", *p); return 2; }
        }
        argi++;
    }

    (void)all; /* -a flag: reserved for showing inactive interfaces */
    const char *ifname = (argi < argc) ? argv[argi] : NULL;
    return show_interface(ifname, short_mode);
}
