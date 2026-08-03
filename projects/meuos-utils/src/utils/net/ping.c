/* ping — ICMP echo 请求工具
 *
 * 用法：ping [-c count] [-i interval] [-s size] [-W timeout] [-q] host
 *
 * 发送 ICMP echo 请求包并等待响应。
 * 使用 raw socket (SOCK_RAW, IPPROTO_ICMP)。
 *
 * --classic: 传统 ping 格式
 */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>

#include "meuos/utils.h"

static volatile int running = 1;
static void sighandler(int sig) { (void)sig; running = 0; }

/* 计算校验和 */
static unsigned short checksum(void *b, int len) {
    unsigned short *buf = b;
    unsigned int sum = 0;
    while (len > 1) {
        sum += *buf++;
        len -= 2;
    }
    if (len == 1)
        sum += *(unsigned char *)buf;
    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);
    return (unsigned short)(~sum);
}

static double now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
}

int main(int argc, char **argv) {
    set_program_name(argv[0]);
    utils_classic_init(argc, argv);

    int count = -1;       /* -c: 无限 */
    double interval = 1.0; /* -i: 秒 */
    int size = 56;        /* -s: ICMP payload 大小 */
    int timeout_ms = 1000; /* -W: 超时毫秒 */
    int quiet = 0;         /* -q */
    int argi = 1;

    while (argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0') {
        if (!strcmp(argv[argi], "--version")) { version(); }
        if (!strcmp(argv[argi], "--help")) {
            fprintf(stderr,
                "Usage: ping [-c count] [-i interval] [-s size] [-W timeout] [-q] host\n"
                "  -c count    Stop after count packets\n"
                "  -i interval Seconds between packets (default 1)\n"
                "  -s size     Packet payload size (default 56)\n"
                "  -W timeout  Reply timeout in ms (default 1000)\n"
                "  -q          Quiet output\n");
            return 0;
        }
        if (!strcmp(argv[argi], "--classic")) { argi++; continue; }
        for (char *p = argv[argi] + 1; *p; p++) {
            switch (*p) {
            case 'c': case 'i': case 's': case 'W':
                if (argi + 1 >= argc) {
                    fprintf(stderr, "ping: -%c requires an argument\n", *p);
                    return 2;
                }
                if (*p == 'c') count = atoi(argv[++argi]);
                else if (*p == 'i') interval = atof(argv[++argi]);
                else if (*p == 's') size = atoi(argv[++argi]);
                else if (*p == 'W') timeout_ms = atoi(argv[++argi]);
                break;
            case 'q': quiet = 1; break;
            default:
                fprintf(stderr, "ping: unknown option -%c\n", *p);
                return 2;
            }
        }
        argi++;
    }

    if (argi >= argc) {
        fprintf(stderr, "ping: usage error: Destination address required\n");
        return 2;
    }

    const char *hostname = argv[argi];

    /* 解析主机地址 */
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_RAW;
    hints.ai_protocol = IPPROTO_ICMP;

    int gai = getaddrinfo(hostname, NULL, &hints, &res);
    if (gai != 0) {
        fprintf(stderr, "ping: %s: %s\n", hostname, gai_strerror(gai));
        return 2;
    }

    char addrbuf[INET_ADDRSTRLEN];
    struct sockaddr_in *sin = (struct sockaddr_in *)res->ai_addr;
    inet_ntop(AF_INET, &sin->sin_addr, addrbuf, sizeof(addrbuf));

    int sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sock < 0) {
        fprintf(stderr, "ping: socket: %s (need root?)\n", strerror(errno));
        freeaddrinfo(res);
        return 2;
    }

    /* 设置接收超时 */
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (!quiet) {
        printf("PING %s (%s) %d(%d) bytes of data.\n",
               hostname, addrbuf, size, size + 28);
    }

    signal(SIGINT, sighandler);

    int sent = 0, recv = 0;
    double rtt_min = 1e9, rtt_max = 0, rtt_sum = 0;
    int seq = 0;

    while (running && (count < 0 || sent < count)) {
        /* 构造 ICMP echo request */
        int pktlen = 8 + size;
        char *pkt = xmalloc(pktlen);
        memset(pkt, 0, pktlen);
        struct icmphdr *icp = (struct icmphdr *)pkt;
        icp->type = ICMP_ECHO;
        icp->code = 0;
        icp->un.echo.id = htons(getpid() & 0xffff);
        icp->un.echo.sequence = htons(seq);
        /* payload 填充时间戳 + 填充字节 */
        struct timeval tv;
        gettimeofday(&tv, NULL);
        if (size >= (int)sizeof(tv))
            memcpy(pkt + 8, &tv, sizeof(tv));
        icp->checksum = 0;
        icp->checksum = checksum(pkt, pktlen);

        double send_time = now_ms();
        int n = sendto(sock, pkt, pktlen, 0, res->ai_addr, res->ai_addrlen);
        free(pkt);
        if (n < 0) {
            fprintf(stderr, "ping: sendto: %s\n", strerror(errno));
            break;
        }
        sent++;

        /* 等待回复 */
        char recvbuf[2048];
        struct sockaddr_in from;
        socklen_t fromlen = sizeof(from);
        n = recvfrom(sock, recvbuf, sizeof(recvbuf), 0,
                     (struct sockaddr *)&from, &fromlen);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (!quiet)
                    printf("Request timeout for icmp_seq %d\n", seq);
            } else {
                fprintf(stderr, "ping: recvfrom: %s\n", strerror(errno));
            }
        } else {
            double recv_time = now_ms();
            double rtt = recv_time - send_time;
            /* 跳过 IP 头 */
            struct iphdr *iph = (struct iphdr *)recvbuf;
            int iphl = iph->ihl * 4;
            struct icmphdr *icp2 = (struct icmphdr *)(recvbuf + iphl);
            if (icp2->type == ICMP_ECHOREPLY) {
                recv++;
                rtt_sum += rtt;
                if (rtt < rtt_min) rtt_min = rtt;
                if (rtt > rtt_max) rtt_max = rtt;
                if (!quiet) {
                    char frombuf[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &from.sin_addr, frombuf, sizeof(frombuf));
                    printf("%d bytes from %s: icmp_seq=%d ttl=%d time=%.1f ms\n",
                           n - iphl, frombuf, seq, iph->ttl, rtt);
                }
            }
        }
        seq++;

        if (count < 0 || sent < count) {
            /* 睡眠 interval 秒 */
            if (interval >= 1.0)
                sleep((unsigned)interval);
            else
                usleep((useconds_t)(interval * 1e6));
        }
    }

    /* 统计 */
    if (!quiet && sent > 0) {
        printf("\n--- %s ping statistics ---\n", hostname);
        printf("%d packets transmitted, %d received, %d%% packet loss\n",
               sent, recv, sent ? (int)((sent - recv) * 100.0 / sent) : 0);
        if (recv > 0) {
            printf("rtt min/avg/max = %.1f/%.1f/%.1f ms\n",
                   rtt_min, rtt_sum / recv, rtt_max);
        }
    }

    close(sock);
    freeaddrinfo(res);
    return (recv > 0) ? 0 : 1;
}
