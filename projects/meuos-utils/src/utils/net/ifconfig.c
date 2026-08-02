/* ifconfig — 网络接口配置查看器
 *
 * 用法：ifconfig [interface]
 *
 * 读取 /proc/net/dev 和 /sys/class/net/ 获取接口信息。
 * 支持 -a 显示全部（含未启用接口）、-s 精简统计模式。
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

static void usage(void) {
    fprintf(stderr,
        "Usage: ifconfig [-a] [-s] [interface]\n"
        "  -a    Show all interfaces (including inactive)\n"
        "  -s    Short list output\n"
        "  --classic  Traditional format\n");
}

static char *mac_bytes(unsigned char *m) {
    static char buf[18];
    snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
             m[0], m[1], m[2], m[3], m[4], m[5]);
    return buf;
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
        char line[512];
        /* 跳过前两行 */
        fgets(line, sizeof(line), f);
        fgets(line, sizeof(line), f);
        while (fgets(line, sizeof(line), f)) {
            char name[64];
            unsigned long rx_bytes, rx_pkt, rx_err, rx_drop, rx_fifo, rx_frame;
            unsigned long rx_compressed, rx_multicast;
            unsigned long tx_bytes, tx_pkt, tx_err, tx_drop, tx_fifo;
            unsigned long tx_colls, tx_carrier, tx_compressed;
            char *p = line;
            while (*p == ' ') p++;
            /* 接口名 */
            char *colon = strchr(p, ':');
            if (!colon) continue;
            size_t nlen = colon - p;
            if (nlen >= sizeof(name)) nlen = sizeof(name) - 1;
            memcpy(name, p, nlen);
            name[nlen] = '\0';
            if (ifname && strcmp(name, ifname) != 0) continue;
            sscanf(colon + 1,
                "%lu %lu %lu %lu %lu %lu %lu %lu %lu "
                "%lu %lu %lu %lu %lu %lu %lu %lu",
                &rx_bytes, &rx_pkt, &rx_err, &rx_drop, &rx_fifo, &rx_frame,
                &rx_compressed, &rx_multicast, &tx_bytes, &tx_pkt, &tx_err,
                &tx_drop, &tx_fifo, &tx_colls, &tx_carrier, &tx_compressed);
            printf("%-10s ", name);
            print_human_bytes(rx_bytes);
            printf("  ");
            print_human_bytes(tx_bytes);
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

    int showed = 0;
    for (ifa = ifa0; ifa; ifa = ifa->ifa_next) {
        if (ifname && strcmp(ifa->ifa_name, ifname) != 0)
            continue;
        if (!ifa->ifa_addr)
            continue;

        /* 只在第一次遇到这个接口时打印头 */
        if (!showed || strcmp(ifa->ifa_name, ifname ? ifname : "") != 0) {
            showed = 1;
            ifname = ifa->ifa_name;
            printf("\n%s: flags=%d", ifa->ifa_name, ifa->ifa_flags);
            int s = socket(AF_INET, SOCK_DGRAM, 0);
            if (s >= 0) {
                struct ifreq ifr;
                memset(&ifr, 0, sizeof(ifr));
                strncpy(ifr.ifr_name, ifa->ifa_name, IFNAMSIZ - 1);
                if (ioctl(s, SIOCGIFMTU, &ifr) == 0)
                    printf("  mtu %d", ifr.ifr_mtu);
                close(s);
            }
            printf("\n");
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
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s >= 0) {
        struct ifreq ifr;
        memset(&ifr, 0, sizeof(ifr));
        strncpy(ifr.ifr_name, ifname ? ifname : "", IFNAMSIZ - 1);
        if (ioctl(s, SIOCGIFHWADDR, &ifr) == 0) {
            printf("    ether %s\n",
                   mac_bytes((unsigned char *)ifr.ifr_hwaddr.sa_data));
        }
        close(s);
    }

    /* 统计信息 */
    FILE *f = fopen("/proc/net/dev", "r");
    if (f) {
        char line[512];
        fgets(line, sizeof(line), f);
        fgets(line, sizeof(line), f);
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
            unsigned long rx_b, rx_p, rx_e, rx_d, rx_f, rx_fr, rx_c, rx_m;
            unsigned long tx_b, tx_p, tx_e, tx_d, tx_f, tx_co, tx_ca, tx_c2;
            sscanf(colon + 1,
                "%lu %lu %lu %lu %lu %lu %lu %lu %lu "
                "%lu %lu %lu %lu %lu %lu %lu %lu",
                &rx_b, &rx_p, &rx_e, &rx_d, &rx_f, &rx_fr, &rx_c, &rx_m,
                &tx_b, &tx_p, &tx_e, &tx_d, &tx_f, &tx_co, &tx_ca, &tx_c2);
            printf("    RX packets %lu  bytes %lu", rx_p, rx_b);
            if (rx_e) printf("  errors %lu", rx_e);
            if (rx_d) printf("  dropped %lu", rx_d);
            printf("\n");
            printf("    TX packets %lu  bytes %lu", tx_p, tx_b);
            if (tx_e) printf("  errors %lu", tx_e);
            if (tx_d) printf("  dropped %lu", tx_d);
            if (tx_co) printf("  collisions %lu", tx_co);
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

    const char *ifname = (argi < argc) ? argv[argi] : NULL;
    return show_interface(ifname, short_mode);
}
