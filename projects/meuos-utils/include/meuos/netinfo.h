/* netinfo.h — 网络信息共享 API
 *
 * 为 ip/ifconfig/route/netstat 等网络工具提供公共的数据获取和格式化接口，
 * 消除跨工具的重复代码（/proc/net/dev 解析、ioctl 封装、MAC/IP 格式化等）。
 *
 * 设计原则：
 *   - 所有函数只依赖 POSIX + Linux 内核 ABI，零 GNU 专有符号
 *   - ioctl 函数接受已打开的 socket fd，避免反复创建/销毁
 *   - 格式化函数返回 static buffer（非线程安全，与 ifconfig/ip 一致）
 */
#ifndef MEUOS_NETINFO_H
#define MEUOS_NETINFO_H

#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * /proc/net/dev 接口统计
 * ============================================================ */

/* 单个接口的收发统计（对应 /proc/net/dev 的一行 16 个字段） */
struct net_dev_stats {
    char name[64];
    unsigned long rx_bytes, rx_packets, rx_errors, rx_dropped;
    unsigned long rx_fifo, rx_frame, rx_compressed, rx_multicast;
    unsigned long tx_bytes, tx_packets, tx_errors, tx_dropped;
    unsigned long tx_fifo, tx_collisions, tx_carrier, tx_compressed;
};

/* 从 /proc/net/dev 读取一行统计信息。
 *   f     — 已打开的 /proc/net/dev FILE*
 *   st    — 输出统计结构
 * 返回 0=成功, -1=EOF/错误。
 * 自动跳过前两行标题行。 */
int netinfo_dev_read_line(FILE *f, struct net_dev_stats *st);

/* ============================================================
 * ioctl 接口信息封装
 * ============================================================ */

/* 接口硬件信息（flags/MTU/MAC） */
struct if_info {
    unsigned int flags;    /* IFF_UP|IFF_BROADCAST|... */
    int mtu;
    unsigned char mac[6];  /* 硬件地址 */
    int has_mac;           /* MAC 是否可用 */
};

/* 通过 ioctl 获取接口的 flags/MTU/MAC。
 *   sock    — 已打开的 AF_INET SOCK_DGRAM socket
 *   ifname  — 接口名（如 "eth0"）
 *   info    — 输出
 * 返回 0=成功, -1=ioctl 失败。 */
int ifinfo_get(int sock, const char *ifname, struct if_info *info);

/* 通过 ioctl 获取接口广播地址。
 *   sock    — 已打开的 AF_INET SOCK_DGRAM socket
 *   ifname  — 接口名
 *   buf     — 输出缓冲区（至少 INET_ADDRSTRLEN 字节）
 *   len     — buf 长度
 * 返回 0=成功, -1=失败/不支持。 */
int ifinfo_get_bcast(int sock, const char *ifname, char *buf, size_t len);

/* ============================================================
 * 格式化函数
 * ============================================================ */

/* 格式化 MAC 地址为 "aa:bb:cc:dd:ee:ff"（返回 static buffer） */
const char *fmt_mac(const unsigned char mac[6]);

/* 打印接口 flags 为 <UP,BROADCAST,RUNNING> 格式到 stdout */
void fmt_if_flags(unsigned int flags);

/* 内核十六进制 IP（小端）→ 点分十进制字符串（返回 static buffer） */
const char *fmt_hex_ip(unsigned int hex);

/* ============================================================
 * CIDR 前缀计算
 * ============================================================ */

/* IPv4 点分十进制掩码字符串 → CIDR 前缀长度（如 "255.255.255.0" → 24） */
int netmask_str_to_prefix_v4(const char *mask_str);

/* IPv4 整数掩码 → CIDR 前缀长度 */
int netmask_to_prefix_v4(unsigned int mask);

/* IPv6 掩码（16 字节）→ CIDR 前缀长度 */
int netmask_to_prefix_v6(const unsigned char mask[16]);

/* ============================================================
 * /proc/net/route 路由表
 * ============================================================ */

/* 单条路由条目（对应 /proc/net/route 的一行） */
struct route_entry {
    char iface[16];       /* 接口名 */
    unsigned int dest;    /* 目的地址（内核小端十六进制） */
    unsigned int gateway; /* 网关 */
    unsigned int mask;    /* 子网掩码 */
    unsigned int flags;   /* 路由 flags */
    unsigned int metric;  /* 优先级 */
    unsigned int ref;     /* 引用计数 */
    unsigned int use;     /* 使用计数 */
};

/* 从 /proc/net/route 读取一行。
 *   f     — 已打开的 /proc/net/route FILE*
 *   e     — 输出路由条目
 * 返回 0=成功, -1=EOF/错误。
 * 自动跳过标题行。 */
int netinfo_route_read_line(FILE *f, struct route_entry *e);

#ifdef __cplusplus
}
#endif

#endif /* MEUOS_NETINFO_H */
