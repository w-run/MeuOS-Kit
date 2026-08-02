/* ip — 网络接口/路由/地址管理工具
 *
 * 用法：
 *   ip addr                     显示地址信息
 *   ip addr show [dev]          显示指定接口地址
 *   ip addr add ADDR dev IFACE  添加地址
 *   ip addr del ADDR dev IFACE  删除地址
 *   ip link show [dev]          显示链路信息
 *   ip link set IFACE up|down   启用/禁用接口
 *   ip route                     显示路由表
 *   ip route show                显示路由表（同上）
 *   ip route add ROUTE via GW [dev IFACE]  添加路由
 *   ip route del ROUTE          删除路由
 *   ip neigh                     显示邻居表(ARP)
 *   ip -s link                   显示统计信息
 *
 * 读取 /proc/net/ 和使用 ioctl/netlink 进行操作。
 *
 * --classic: 传统格式
 */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <ifaddrs.h>
#include <netdb.h>

#include "meuos/utils.h"

static void usage(void) {
    fprintf(stderr,
        "Usage: ip [options] OBJECT { COMMAND | help }\n"
        "OBJECT := { addr | link | route | neigh }\n"
        "OPTIONS := { -s | -j | -f FAMILY | -4 | -6 }\n"
        "\n"
        "  ip addr [show [dev IFACE]]\n"
        "  ip addr add ADDR/PREFIX dev IFACE\n"
        "  ip addr del ADDR/PREFIX dev IFACE\n"
        "  ip link [show [dev IFACE]]\n"
        "  ip link set IFACE {up|down|mtu N|address MAC}\n"
        "  ip route [show]\n"
        "  ip route add DEST via GW [dev IFACE] [metric N]\n"
        "  ip route del DEST\n"
        "  ip neigh [show]\n"
        "  ip -s link [show]\n"
        "  --classic  Traditional format\n");
}

/* 打印接口 flags（共享给 addr_show 和 link_show） */
static void print_flags(unsigned int flags) {
    printf("<");
    int first = 1;
    if (flags & IFF_UP)         { printf("%sUP", first ? "" : ","); first = 0; }
    if (flags & IFF_BROADCAST)   { printf("%sBROADCAST", first ? "" : ","); first = 0; }
    if (flags & IFF_LOOPBACK)    { printf("%sLOOPBACK", first ? "" : ","); first = 0; }
    if (flags & IFF_RUNNING)     { printf("%sRUNNING", first ? "" : ","); first = 0; }
    if (flags & IFF_NOARP)       { printf("%sNOARP", first ? "" : ","); first = 0; }
    if (flags & IFF_MULTICAST)   { printf("%sMULTICAST", first ? "" : ","); first = 0; }
    if (flags & IFF_PROMISC)     { printf("%sPROMISC", first ? "" : ","); first = 0; }
    printf(">");
}

/* === addr show === */
static int cmd_addr_show(const char *ifname) {
    struct ifaddrs *ifa, *ifa0;
    if (getifaddrs(&ifa0) != 0) {
        perror("ip: getifaddrs");
        return 1;
    }

    /* 单一 socket 用于所有 ioctl 调用 */
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) {
        perror("ip: socket");
        freeifaddrs(ifa0);
        return 1;
    }

    char shown_names[64][IFNAMSIZ];
    int nshown = 0;

    for (ifa = ifa0; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) continue;
        if (ifname && strcmp(ifa->ifa_name, ifname) != 0) continue;

        /* 检查是否已显示过该接口 */
        int already = 0;
        for (int i = 0; i < nshown; i++) {
            if (strcmp(shown_names[i], ifa->ifa_name) == 0) {
                already = 1;
                break;
            }
        }
        if (!already) {
            if (nshown < 64) {
                snprintf(shown_names[nshown], sizeof(shown_names[nshown]),
                         "%s", ifa->ifa_name);
                nshown++;
            }

            /* 获取 ifindex + MTU via ioctl */
            struct ifreq ifr;
            memset(&ifr, 0, sizeof(ifr));
            snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", ifa->ifa_name);
            int ifindex = nshown;
            int mtu = 0;
            if (ioctl(s, SIOCGIFINDEX, &ifr) == 0) ifindex = ifr.ifr_ifindex;
            if (ioctl(s, SIOCGIFMTU, &ifr) == 0) mtu = ifr.ifr_mtu;

            /* 接口头 */
            printf("%d: %s: ", ifindex, ifa->ifa_name);
            print_flags(ifa->ifa_flags);
            printf(" mtu %d state %s\n", mtu,
                   (ifa->ifa_flags & IFF_UP) ? "UP" : "DOWN");

            /* 获取 MAC 地址 */
            memset(&ifr, 0, sizeof(ifr));
            snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", ifa->ifa_name);
            if (ioctl(s, SIOCGIFHWADDR, &ifr) == 0) {
                unsigned char *m = (unsigned char *)ifr.ifr_hwaddr.sa_data;
                printf("    link/ether %02x:%02x:%02x:%02x:%02x:%02x",
                       m[0], m[1], m[2], m[3], m[4], m[5]);
                if (ifa->ifa_flags & IFF_BROADCAST) {
                    struct ifreq br;
                    memset(&br, 0, sizeof(br));
                    snprintf(br.ifr_name, sizeof(br.ifr_name), "%s", ifa->ifa_name);
                    if (ioctl(s, SIOCGIFBRDADDR, &br) == 0) {
                        struct sockaddr_in *sa = (struct sockaddr_in *)&br.ifr_broadaddr;
                        char bbuf[INET_ADDRSTRLEN];
                        inet_ntop(AF_INET, &sa->sin_addr, bbuf, sizeof(bbuf));
                        printf(" brd %s", bbuf);
                    }
                }
                printf("\n");
            }
        }

        /* IP 地址 */
        char host[NI_MAXHOST];
        socklen_t salen;
        if (ifa->ifa_addr->sa_family == AF_INET) {
            salen = sizeof(struct sockaddr_in);
        } else if (ifa->ifa_addr->sa_family == AF_INET6) {
            salen = sizeof(struct sockaddr_in6);
        } else {
            continue;
        }

        if (getnameinfo(ifa->ifa_addr, salen, host, sizeof(host),
                        NULL, 0, NI_NUMERICHOST) == 0) {
            const char *fam = (ifa->ifa_addr->sa_family == AF_INET) ? "inet" : "inet6";

            /* 计算前缀长度 */
            int prefix = 0;
            if (ifa->ifa_netmask) {
                if (ifa->ifa_netmask->sa_family == AF_INET) {
                    struct sockaddr_in *nm = (struct sockaddr_in *)ifa->ifa_netmask;
                    unsigned long mask = ntohl(nm->sin_addr.s_addr);
                    while (mask & 0x80000000) { prefix++; mask <<= 1; }
                } else if (ifa->ifa_netmask->sa_family == AF_INET6) {
                    struct sockaddr_in6 *nm = (struct sockaddr_in6 *)ifa->ifa_netmask;
                    for (int i = 0; i < 16; i++) {
                        unsigned char b = nm->sin6_addr.s6_addr[i];
                        if (b == 0xff) prefix += 8;
                        else if (b == 0) break;
                        else { while (b & 0x80) { prefix++; b <<= 1; } break; }
                    }
                }
            }
            printf("    %s %s/%d", fam, host, prefix);

            /* 广播地址 */
            if (ifa->ifa_flags & IFF_BROADCAST && ifa->ifa_broadaddr) {
                char bcast[NI_MAXHOST];
                if (getnameinfo(ifa->ifa_broadaddr, salen, bcast, sizeof(bcast),
                                NULL, 0, NI_NUMERICHOST) == 0)
                    printf(" brd %s", bcast);
            }
            /* scope */
            if (ifa->ifa_addr->sa_family == AF_INET6) {
                struct sockaddr_in6 *s6 = (struct sockaddr_in6 *)ifa->ifa_addr;
                if (IN6_IS_ADDR_LINKLOCAL(&s6->sin6_addr))
                    printf(" scope link");
                else if (IN6_IS_ADDR_LOOPBACK(&s6->sin6_addr))
                    printf(" scope host");
            }
            printf("\n");
        }
    }

    close(s);
    freeifaddrs(ifa0);
    return 0;
}

/* === link show === */
static int cmd_link_show(const char *ifname, int show_stats) {
    FILE *f = fopen("/proc/net/dev", "r");
    if (!f) {
        perror("ip: /proc/net/dev");
        return 1;
    }
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) {
        perror("ip: socket");
        fclose(f);
        return 1;
    }

    char line[512];
    /* 跳过前两行标题 */
    if (!fgets(line, sizeof(line), f)) goto done;
    if (!fgets(line, sizeof(line), f)) goto done;

    int idx = 1;
    while (fgets(line, sizeof(line), f)) {
        char name[64];
        char *p = line;
        while (*p == ' ') p++;
        char *colon = strchr(p, ':');
        if (!colon) continue;
        size_t nlen = colon - p;
        if (nlen >= sizeof(name)) nlen = sizeof(name) - 1;
        memcpy(name, p, nlen);
        name[nlen] = '\0';
        if (ifname && strcmp(name, ifname) != 0) continue;

        unsigned long rx_b, rx_p, rx_e, rx_d, rx_fi, rx_fr, rx_c, rx_m;
        unsigned long tx_b, tx_p, tx_e, tx_d, tx_fi, tx_co, tx_ca, tx_c2;
        sscanf(colon + 1,
            "%lu %lu %lu %lu %lu %lu %lu %lu %lu "
            "%lu %lu %lu %lu %lu %lu %lu %lu",
            &rx_b, &rx_p, &rx_e, &rx_d, &rx_fi, &rx_fr, &rx_c, &rx_m,
            &tx_b, &tx_p, &tx_e, &tx_d, &tx_fi, &tx_co, &tx_ca, &tx_c2);

        /* 获取 flags + MTU + MAC via 单一 socket */
        struct ifreq ifr;
        memset(&ifr, 0, sizeof(ifr));
        snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", name);
        unsigned int flags = 0;
        int mtu = 0;
        if (ioctl(s, SIOCGIFFLAGS, &ifr) == 0) flags = ifr.ifr_flags;
        memset(&ifr, 0, sizeof(ifr));
        snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", name);
        if (ioctl(s, SIOCGIFMTU, &ifr) == 0) mtu = ifr.ifr_mtu;

        printf("%d: %s: ", idx, name);
        print_flags(flags);
        printf(" mtu %d state %s\n", mtu, (flags & IFF_UP) ? "UP" : "DOWN");

        /* 获取 MAC */
        memset(&ifr, 0, sizeof(ifr));
        snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", name);
        if (ioctl(s, SIOCGIFHWADDR, &ifr) == 0) {
            unsigned char *m = (unsigned char *)ifr.ifr_hwaddr.sa_data;
            printf("    link/ether %02x:%02x:%02x:%02x:%02x:%02x\n",
                   m[0], m[1], m[2], m[3], m[4], m[5]);
        }

        if (show_stats) {
            printf("    RX: bytes packets errors dropped\n"
                   "    %lu %lu %lu %lu\n"
                   "    TX: bytes packets errors dropped\n"
                   "    %lu %lu %lu %lu\n",
                   rx_b, rx_p, rx_e, rx_d,
                   tx_b, tx_p, tx_e, tx_d);
        }
        idx++;
    }
done:
    close(s);
    fclose(f);
    return 0;
}

/* === link set === */
static int cmd_link_set(const char *ifname, const char *attr, const char *val) {
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) { perror("ip: socket"); return 1; }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", ifname);

    if (!strcmp(attr, "up")) {
        if (ioctl(s, SIOCGIFFLAGS, &ifr) < 0) { perror("ip: SIOCGIFFLAGS"); close(s); return 1; }
        ifr.ifr_flags |= IFF_UP;
        if (ioctl(s, SIOCSIFFLAGS, &ifr) < 0) { perror("ip: SIOCSIFFLAGS"); close(s); return 1; }
    } else if (!strcmp(attr, "down")) {
        if (ioctl(s, SIOCGIFFLAGS, &ifr) < 0) { perror("ip: SIOCGIFFLAGS"); close(s); return 1; }
        ifr.ifr_flags &= ~IFF_UP;
        if (ioctl(s, SIOCSIFFLAGS, &ifr) < 0) { perror("ip: SIOCSIFFLAGS"); close(s); return 1; }
    } else if (!strcmp(attr, "mtu")) {
        if (!val) { fprintf(stderr, "ip: link set mtu: missing value\n"); close(s); return 1; }
        ifr.ifr_mtu = atoi(val);
        if (ioctl(s, SIOCSIFMTU, &ifr) < 0) { perror("ip: SIOCSIFMTU"); close(s); return 1; }
    } else {
        fprintf(stderr, "ip: unknown link attribute: %s\n", attr);
        close(s);
        return 1;
    }
    close(s);
    return 0;
}

/* 计算前缀长度：掩码 -> CIDR 前缀 */
static int mask_to_prefix(unsigned int mask) {
    int prefix = 0;
    while (mask & 0x80000000) { prefix++; mask <<= 1; }
    return prefix;
}

/* === route show === */
static int cmd_route_show(void) {
    FILE *f = fopen("/proc/net/route", "r");
    if (!f) { perror("ip: /proc/net/route"); return 1; }
    char line[512];
    fgets(line, sizeof(line), f); /* skip header */
    while (fgets(line, sizeof(line), f)) {
        char iface[16];
        unsigned int dest, gw, mask;
        unsigned int flags, metric;
        if (sscanf(line, "%15s %x %x %x %x %x",
                   iface, &dest, &gw, &mask, &flags, &metric) < 6)
            continue;
        struct in_addr ga;
        ga.s_addr = gw;
        char gb[32];
        inet_ntop(AF_INET, &ga, gb, sizeof(gb));

        if (dest == 0) {
            /* default route */
            printf("default via %s dev %s", gb, iface);
        } else {
            int prefix = mask_to_prefix(ntohl(mask));
            struct in_addr da;
            da.s_addr = dest;
            char db[32];
            inet_ntop(AF_INET, &da, db, sizeof(db));
            printf("%s/%d", db, prefix);
            if (gw != 0) printf(" via %s", gb);
            printf(" dev %s", iface);
        }
        if (metric > 0) printf(" metric %u", metric);
        printf("\n");
    }
    fclose(f);
    return 0;
}

/* === neigh show === */
static int cmd_neigh_show(void) {
    FILE *f = fopen("/proc/net/arp", "r");
    if (!f) { perror("ip: /proc/net/arp"); return 1; }
    char line[512];
    fgets(line, sizeof(line), f); /* skip header */
    while (fgets(line, sizeof(line), f)) {
        char ip[32], hw[32], dev[32], flags[8];
        /* IP address HW type Flags HW address Mask Device */
        if (sscanf(line, "%31s %*s %7s %31s %*s %31s", ip, flags, hw, dev) >= 4) {
            /* 格式化 MAC */
            if (strcmp(hw, "00:00:00:00:00:00") == 0) {
                printf("%s dev %s FAILED\n", ip, dev);
            } else {
                printf("%s dev %s lladdr %s %s\n", ip, dev, hw,
                       (strstr(flags, "M")) ? "STALE" : "REACHABLE");
            }
        }
    }
    fclose(f);
    return 0;
}

int main(int argc, char **argv) {
    set_program_name(argv[0]);
    utils_classic_init(argc, argv);

    int show_stats = 0;
    int argi = 1;

    /* 全局选项 */
    while (argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0') {
        if (!strcmp(argv[argi], "--version")) { version(); }
        if (!strcmp(argv[argi], "--help")) { usage(); return 0; }
        if (!strcmp(argv[argi], "--classic")) { argi++; continue; }
        if (!strcmp(argv[argi], "-4")) { argi++; continue; }
        if (!strcmp(argv[argi], "-6")) { argi++; continue; }
        /* -f FAMILY：消耗下一个参数 */
        if (!strcmp(argv[argi], "-f") && argi + 1 < argc) { argi += 2; continue; }
        char *p = argv[argi] + 1;
        while (*p) {
            switch (*p) {
            case 's': show_stats = 1; break;
            case 'j': /* JSON 输出（简化：同文本） */ break;
            default: break;
            }
            p++;
        }
        argi++;
    }

    if (argi >= argc) {
        /* 无对象：默认 addr show */
        return cmd_addr_show(NULL);
    }

    const char *obj = argv[argi++];

    if (!strcmp(obj, "addr") || !strcmp(obj, "a") || !strcmp(obj, "address")) {
        /* ip addr [show [dev IFACE]] | [add ADDR dev IFACE] | [del ADDR dev IFACE] */
        if (argi >= argc || !strcmp(argv[argi], "show") || !strcmp(argv[argi], "list")) {
            const char *ifname = NULL;
            if (argi < argc && !strcmp(argv[argi], "show")) argi++;
            if (argi < argc && !strcmp(argv[argi], "dev") && argi + 1 < argc) {
                ifname = argv[argi + 1];
            }
            return cmd_addr_show(ifname);
        }
        if (!strcmp(argv[argi], "add") || !strcmp(argv[argi], "del")) {
            fprintf(stderr, "ip: addr add/del requires netlink (not yet supported)\n");
            return 1;
        }
    }

    if (!strcmp(obj, "link") || !strcmp(obj, "l")) {
        if (argi >= argc || !strcmp(argv[argi], "show") || !strcmp(argv[argi], "list")) {
            const char *ifname = NULL;
            if (argi < argc && !strcmp(argv[argi], "show")) argi++;
            if (argi < argc && !strcmp(argv[argi], "dev") && argi + 1 < argc) {
                ifname = argv[argi + 1];
            } else if (argi < argc) {
                ifname = argv[argi];
            }
            return cmd_link_show(ifname, show_stats);
        }
        if (!strcmp(argv[argi], "set")) {
            argi++;
            if (argi >= argc) { fprintf(stderr, "ip: link set: missing interface\n"); return 2; }
            const char *ifname = argv[argi++];
            if (argi >= argc) { fprintf(stderr, "ip: link set: missing attribute\n"); return 2; }
            const char *attr = argv[argi++];
            const char *val = (argi < argc) ? argv[argi] : NULL;
            return cmd_link_set(ifname, attr, val);
        }
    }

    if (!strcmp(obj, "route") || !strcmp(obj, "r")) {
        if (argi >= argc || !strcmp(argv[argi], "show") || !strcmp(argv[argi], "list")) {
            return cmd_route_show();
        }
        if (!strcmp(argv[argi], "add")) {
            fprintf(stderr, "ip: route add requires netlink (not yet supported)\n");
            return 1;
        }
        if (!strcmp(argv[argi], "del")) {
            fprintf(stderr, "ip: route del requires netlink (not yet supported)\n");
            return 1;
        }
    }

    if (!strcmp(obj, "neigh") || !strcmp(obj, "n") || !strcmp(obj, "neighbor")) {
        return cmd_neigh_show();
    }

    fprintf(stderr, "ip: unknown object \"%s\"\n", obj);
    usage();
    return 2;
}
