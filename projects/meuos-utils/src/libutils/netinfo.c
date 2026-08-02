/* netinfo.c — 网络信息共享实现
 *
 * 为 ip/ifconfig/route/netstat 提供公共的数据获取和格式化功能。
 * 消除以下重复代码：
 *   - /proc/net/dev 的 16 字段 sscanf 解析（原 4 处重复）
 *   - 接口名提取（skip spaces + find colon）（原 5 处重复）
 *   - ioctl SIOCGIFHWADDR/SIOCGIFMTU/SIOCGIFFLAGS 封装（原 3 处重复）
 *   - MAC 地址格式化 %02x:...（原 3 处重复）
 *   - /proc/net/route 解析（原 3 处重复）
 *   - hex IP → dotted decimal 转换（原 3 处重复）
 *   - 掩码 → CIDR 前缀长度（原 2 处重复）
 */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <arpa/inet.h>

#include "meuos/netinfo.h"

/* ============================================================
 * /proc/net/dev 接口统计
 * ============================================================ */

int netinfo_dev_read_line(FILE *f, struct net_dev_stats *st) {
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        /* 跳过标题行（含 "Inter-|" 和 "face |" 的行） */
        if (strstr(line, "Inter-") || strstr(line, "face |"))
            continue;

        /* 提取接口名：跳过前导空格，找到冒号分隔 */
        char *p = line;
        while (*p == ' ') p++;
        char *colon = strchr(p, ':');
        if (!colon) continue;
        size_t nlen = colon - p;
        if (nlen >= sizeof(st->name)) nlen = sizeof(st->name) - 1;
        memcpy(st->name, p, nlen);
        st->name[nlen] = '\0';

        /* 解析 16 个字段: RX(8) + TX(8) */
        if (sscanf(colon + 1,
                "%lu %lu %lu %lu %lu %lu %lu %lu "
                "%lu %lu %lu %lu %lu %lu %lu %lu",
                &st->rx_bytes, &st->rx_packets, &st->rx_errors,
                &st->rx_dropped, &st->rx_fifo, &st->rx_frame,
                &st->rx_compressed, &st->rx_multicast,
                &st->tx_bytes, &st->tx_packets, &st->tx_errors,
                &st->tx_dropped, &st->tx_fifo, &st->tx_collisions,
                &st->tx_carrier, &st->tx_compressed) < 16)
            continue;

        return 0;
    }
    return -1;
}

/* ============================================================
 * ioctl 接口信息封装
 * ============================================================ */

int ifinfo_get(int sock, const char *ifname, struct if_info *info) {
    struct ifreq ifr;
    memset(info, 0, sizeof(*info));

    /* flags */
    memset(&ifr, 0, sizeof(ifr));
    snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", ifname);
    if (ioctl(sock, SIOCGIFFLAGS, &ifr) != 0)
        return -1;
    info->flags = ifr.ifr_flags;

    /* MTU */
    memset(&ifr, 0, sizeof(ifr));
    snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", ifname);
    if (ioctl(sock, SIOCGIFMTU, &ifr) == 0)
        info->mtu = ifr.ifr_mtu;

    /* MAC */
    memset(&ifr, 0, sizeof(ifr));
    snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", ifname);
    if (ioctl(sock, SIOCGIFHWADDR, &ifr) == 0) {
        memcpy(info->mac, ifr.ifr_hwaddr.sa_data, 6);
        info->has_mac = 1;
    }

    return 0;
}

int ifinfo_get_bcast(int sock, const char *ifname, char *buf, size_t len) {
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", ifname);
    if (ioctl(sock, SIOCGIFBRDADDR, &ifr) != 0)
        return -1;
    struct sockaddr_in *sa = (struct sockaddr_in *)&ifr.ifr_broadaddr;
    if (!inet_ntop(AF_INET, &sa->sin_addr, buf, len))
        return -1;
    return 0;
}

/* ============================================================
 * 格式化函数
 * ============================================================ */

const char *fmt_mac(const unsigned char mac[6]) {
    static char buf[18];
    snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return buf;
}

void fmt_if_flags(unsigned int flags) {
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

const char *fmt_hex_ip(unsigned int hex) {
    static char buf[INET_ADDRSTRLEN];
    struct in_addr addr;
    addr.s_addr = hex; /* 内核存储为小端，与 in_addr 一致 */
    inet_ntop(AF_INET, &addr, buf, sizeof(buf));
    return buf;
}

/* ============================================================
 * CIDR 前缀计算
 * ============================================================ */

int netmask_str_to_prefix_v4(const char *mask_str) {
    struct in_addr addr;
    if (!inet_pton(AF_INET, mask_str, &addr))
        return -1;
    return netmask_to_prefix_v4(ntohl(addr.s_addr));
}

int netmask_to_prefix_v4(unsigned int mask) {
    int prefix = 0;
    while (mask & 0x80000000) { prefix++; mask <<= 1; }
    return prefix;
}

int netmask_to_prefix_v6(const unsigned char mask[16]) {
    int prefix = 0;
    for (int i = 0; i < 16; i++) {
        unsigned char b = mask[i];
        if (b == 0xff) prefix += 8;
        else if (b == 0) break;
        else { while (b & 0x80) { prefix++; b <<= 1; } break; }
    }
    return prefix;
}

/* ============================================================
 * /proc/net/route 路由表
 * ============================================================ */

int netinfo_route_read_line(FILE *f, struct route_entry *e) {
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        /* 跳过标题行（含 "Iface"） */
        if (strstr(line, "Iface"))
            continue;
        /* 解析: Iface Destination Gateway Flags RefCnt Use Metric Mask */
        if (sscanf(line, "%15s %x %x %x %u %u %u %x",
                   e->iface, &e->dest, &e->gateway, &e->flags,
                   &e->ref, &e->use, &e->metric, &e->mask) < 8)
            continue;
        return 0;
    }
    return -1;
}
