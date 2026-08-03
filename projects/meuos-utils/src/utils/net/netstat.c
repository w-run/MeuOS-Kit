/* netstat — 网络连接与统计查看器
 *
 * 用法：netstat [-t] [-u] [-w] [-x] [-l] [-n] [-a] [-r] [-i] [-s]
 *
 * -t: TCP  -u: UDP  -w: RAW  -x: Unix
 * -l: 仅监听  -n: 数字模式  -a: 全部  -r: 路由表  -i: 接口  -s: 统计
 *
 * 默认：-t -u -a（类似 netstat 不带参数）
 * 共享代码通过 netinfo 模块复用（与 ip/ifconfig/route 共用）。
 *
 * --classic: 传统格式
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
        "Usage: netstat [-tuxwla] [-n] [-r] [-i] [-s]\n"
        "  -t    TCP sockets\n"
        "  -u    UDP sockets\n"
        "  -x    Unix sockets\n"
        "  -w    Raw sockets\n"
        "  -l    Listening only\n"
        "  -a    All (default)\n"
        "  -n    Numeric (no name resolution)\n"
        "  -r    Show routing table\n"
        "  -i    Show interface stats\n"
        "  -s    Show per-protocol statistics\n"
        "  --classic  Traditional format\n");
}

/* TCP 状态码 -> 名称 */
static const char *tcp_state(int state) {
    switch (state) {
    case 1: return "ESTABLISHED";
    case 2: return "SYN_SENT";
    case 3: return "SYN_RECV";
    case 4: return "FIN_WAIT1";
    case 5: return "FIN_WAIT2";
    case 6: return "TIME_WAIT";
    case 7: return "CLOSED";
    case 8: return "CLOSE_WAIT";
    case 9: return "LAST_ACK";
    case 10: return "LISTEN";
    case 11: return "CLOSING";
    default: return "UNKNOWN";
    }
}

/* 将 0xHHHHHHHH:port 格式的内核地址转为可读 */
static void parse_addr(const char *str, char *host, size_t hostlen, int *port) {
    unsigned int ip_hex, port_val;
    if (sscanf(str, "%X:%X", &ip_hex, &port_val) == 2) {
        struct in_addr addr;
        addr.s_addr = htonl(ip_hex);
        inet_ntop(AF_INET, &addr, host, hostlen);
        *port = (int)port_val;
    } else {
        snprintf(host, hostlen, "?");
        *port = 0;
    }
}

static void show_tcp(int listening_only, int numeric) {
    FILE *f = fopen("/proc/net/tcp", "r");
    if (!f) return;
    char line[512];
    printf("Proto Recv-Q Send-Q Local Address           Foreign Address         State\n");
    fgets(line, sizeof(line), f); /* skip header */
    while (fgets(line, sizeof(line), f)) {
        int sl, local, rem, st, txq, rxq, uid;
        char laddr[64], raddr[64];
        char tmp_l[64], tmp_r[64];
        int lport, rport;
        /* 格式: sl local rem st tx:rx ... uid */
        if (sscanf(line, "%d: %63[0-9A-Fa-f:] %63[0-9A-Fa-f:] %X",
                   &sl, tmp_l, tmp_r, &st) < 4)
            continue;
        parse_addr(tmp_l, laddr, sizeof(laddr), &lport);
        parse_addr(tmp_r, raddr, sizeof(raddr), &rport);
        if (listening_only && st != 10) continue;  /* 10 = LISTEN */
        printf("tcp   ");
        /* Recv-Q Send-Q from tx:rx field */
        char *p = strstr(line, tmp_r);
        if (p) {
            p += strlen(tmp_r);
            unsigned int q1, q2;
            if (sscanf(p, " %X:%X", &q1, &q2) == 2)
                printf("%-6u %-6u ", q1, q2);
            else
                printf("0      0      ");
        } else {
            printf("0      0      ");
        }
        printf("%-15s:%-5d  %-15s:%-5d  %s\n",
               laddr, lport, raddr, rport, tcp_state(st));
    }
    fclose(f);
}

static void show_udp(int listening_only, int numeric) {
    FILE *f = fopen("/proc/net/udp", "r");
    if (!f) return;
    char line[512];
    fgets(line, sizeof(line), f); /* skip header */
    while (fgets(line, sizeof(line), f)) {
        int sl, st;
        char tmp_l[64], tmp_r[64];
        char laddr[64], raddr[64];
        int lport, rport;
        if (sscanf(line, "%d: %63[0-9A-Fa-f:] %63[0-9A-Fa-f:] %d",
                   &sl, tmp_l, tmp_r, &st) < 4)
            continue;
        parse_addr(tmp_l, laddr, sizeof(laddr), &lport);
        parse_addr(tmp_r, raddr, sizeof(raddr), &rport);
        if (listening_only && st != 7) continue;  /* 7 =未绑定 */
        printf("udp   0      0      %-15s:%-5d  %-15s:%-5d\n",
               laddr, lport, raddr, rport);
    }
    fclose(f);
}

static void show_unix(void) {
    FILE *f = fopen("/proc/net/unix", "r");
    if (!f) return;
    char line[1024];
    printf("Proto RefCnt Flags       Type     State    I-Node  Path\n");
    fgets(line, sizeof(line), f); /* skip header */
    while (fgets(line, sizeof(line), f)) {
        unsigned long refcnt, flags, type, state, inode;
        char path[512] = "";
        /* 格式: Num RefCnt Protocol Flags Type St Inode Path */
        if (sscanf(line, "%*x %lu %*x %lu %lu %lu %lu %511[^\n]",
                   &refcnt, &flags, &type, &state, &inode, path) >= 5) {
            const char *tstr = (type == 1) ? "STREAM" : (type == 2) ? "DGRAM" : "?";
            const char *sstr = (state == 1) ? "CONNECTED" : "UNCONNECTED";
            printf("unix  %-5ld   %-10s %-8s %-8s %-7ld %s\n",
                   refcnt, "", tstr, sstr, inode,
                   path[0] ? path : "@");
        }
    }
    fclose(f);
}

static void show_route(void) {
    FILE *f = fopen("/proc/net/route", "r");
    if (!f) return;
    printf("Kernel IP routing table\n");
    printf("%-16s %-16s %-16s %-6s %-6s %s\n",
           "Destination", "Gateway", "Genmask", "Flags", "Metric", "Iface");
    struct route_entry e;
    while (netinfo_route_read_line(f, &e) == 0) {
        printf("%-16s %-16s %-16s %-6x %-6u %s\n",
               fmt_hex_ip(e.dest), fmt_hex_ip(e.gateway), fmt_hex_ip(e.mask),
               e.flags, e.metric, e.iface);
    }
    fclose(f);
}

static void show_ifaces(void) {
    FILE *f = fopen("/proc/net/dev", "r");
    if (!f) return;
    printf("%-12s %-10s %-10s %-10s %-10s %-10s %-10s\n",
           "Iface", "RXbytes", "RXpkts", "RXerr", "TXbytes", "TXpkts", "TXerr");
    struct net_dev_stats st;
    while (netinfo_dev_read_line(f, &st) == 0) {
        printf("%-12s %-10lu %-10lu %-10lu %-10lu %-10lu %-10lu\n",
               st.name, st.rx_bytes, st.rx_packets, st.rx_errors,
               st.tx_bytes, st.tx_packets, st.tx_errors);
    }
    fclose(f);
}

static void show_stats(void) {
    /* /proc/net/snmp 含 IP/TCP/UDP/ICMP 统计 */
    FILE *f = fopen("/proc/net/snmp", "r");
    if (!f) return;
    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        /* 交替行：标签行 + 数值行 */
        if (strstr(line, "Tcp:") || strstr(line, "Udp:") ||
            strstr(line, "Ip:") || strstr(line, "Icmp:")) {
            char *newline = strchr(line, '\n');
            if (newline) *newline = '\0';
            printf("%s\n", line);
            if (fgets(line, sizeof(line), f)) {
                newline = strchr(line, '\n');
                if (newline) *newline = '\0';
                printf("%s\n", line);
            }
        }
    }
    fclose(f);
}

int main(int argc, char **argv) {
    set_program_name(argv[0]);
    utils_classic_init(argc, argv);

    int show_tcp_flag = 0, show_udp_flag = 0, show_unix_flag = 0;
    int show_raw_flag = 0, listening = 0, numeric = 0;
    int show_route_flag = 0, show_iface_flag = 0, show_stats_flag = 0;
    int argi = 1;
    while (argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0') {
        if (!strcmp(argv[argi], "--version")) { version(); }
        if (!strcmp(argv[argi], "--help")) { usage(); return 0; }
        if (!strcmp(argv[argi], "--classic")) { argi++; continue; }
        for (char *p = argv[argi] + 1; *p; p++) {
            switch (*p) {
            case 't': show_tcp_flag = 1; break;
            case 'u': show_udp_flag = 1; break;
            case 'x': show_unix_flag = 1; break;
            case 'w': show_raw_flag = 1; break;
            case 'l': listening = 1; break;
            case 'n': numeric = 1; break;
            case 'a': break;  /* -a: show all (default) */
            case 'r': show_route_flag = 1; break;
            case 'i': show_iface_flag = 1; break;
            case 's': show_stats_flag = 1; break;
            default:
                fprintf(stderr, "netstat: unknown option -%c\n", *p);
                return 2;
            }
        }
        argi++;
    }

    /* 默认行为：显示 TCP + UDP + Unix */
    if (!show_tcp_flag && !show_udp_flag && !show_unix_flag &&
        !show_raw_flag && !show_route_flag && !show_iface_flag &&
        !show_stats_flag) {
        show_tcp_flag = 1;
        show_udp_flag = 1;
        show_unix_flag = 1;
    }

    if (show_route_flag) show_route();
    if (show_iface_flag) show_ifaces();
    if (show_stats_flag) show_stats();
    if (show_tcp_flag) show_tcp(listening, numeric);
    if (show_udp_flag) show_udp(listening, numeric);
    if (show_unix_flag) show_unix();
    return 0;
}
