/* nslookup — DNS 查询工具
 *
 * 用法：nslookup [-type=T] [-timeout=N] [-debug] host [server]
 *
 * 查询 DNS 记录，支持 A/AAAA/MX/NS/CNAME/TXT/PTR/SOA 记录类型。
 * 默认查询 A 记录。指定 server 时使用自定义 DNS 服务器。
 * 不依赖 glibc <resolv.h> / <arpa/nameser.h>，自行构造 DNS 报文。
 *
 * --classic: 传统 nslookup 格式
 */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "meuos/utils.h"

/* DNS 记录类型常量（自定义，不依赖 <arpa/nameser.h>） */
#define DNS_TYPE_A      1
#define DNS_TYPE_NS     2
#define DNS_TYPE_CNAME  5
#define DNS_TYPE_SOA    6
#define DNS_TYPE_PTR    12
#define DNS_TYPE_MX     15
#define DNS_TYPE_TXT    16
#define DNS_TYPE_AAAA   28
#define DNS_TYPE_ANY    255
#define DNS_CLASS_IN    1

/* DNS rcode */
#define RCODE_NOERROR   0
#define RCODE_FORMERR   1
#define RCODE_SERVFAIL  2
#define RCODE_NXDOMAIN  3
#define RCODE_REFUSED   5

static void usage(void) {
    fprintf(stderr,
        "Usage: nslookup [-type=T] [-timeout=N] [-debug] host [server]\n"
        "  -type=T      Record type: A, AAAA, MX, NS, CNAME, TXT, PTR, SOA, ANY\n"
        "  -timeout=N   Query timeout in seconds (default 5)\n"
        "  -debug       Enable debug output\n"
        "  --classic    Traditional format\n");
}

/* 记录类型名称 -> 编号 */
static int type_from_name(const char *name) {
    if (!name) return DNS_TYPE_A;
    if (!strcasecmp(name, "A")) return DNS_TYPE_A;
    if (!strcasecmp(name, "AAAA")) return DNS_TYPE_AAAA;
    if (!strcasecmp(name, "MX")) return DNS_TYPE_MX;
    if (!strcasecmp(name, "NS")) return DNS_TYPE_NS;
    if (!strcasecmp(name, "CNAME")) return DNS_TYPE_CNAME;
    if (!strcasecmp(name, "TXT")) return DNS_TYPE_TXT;
    if (!strcasecmp(name, "PTR")) return DNS_TYPE_PTR;
    if (!strcasecmp(name, "SOA")) return DNS_TYPE_SOA;
    if (!strcasecmp(name, "ANY")) return DNS_TYPE_ANY;
    return -1;
}

static const char *type_to_name(int type) {
    switch (type) {
    case DNS_TYPE_A:      return "A";
    case DNS_TYPE_AAAA:  return "AAAA";
    case DNS_TYPE_MX:     return "MX";
    case DNS_TYPE_NS:     return "NS";
    case DNS_TYPE_CNAME:  return "CNAME";
    case DNS_TYPE_TXT:    return "TXT";
    case DNS_TYPE_PTR:    return "PTR";
    case DNS_TYPE_SOA:    return "SOA";
    case DNS_TYPE_ANY:    return "ANY";
    default:              return "?";
    }
}

/* 解压缩 DNS 域名，跟随压缩指针。
 * 安全措施：限制跳转次数（防循环指针）和输出长度（防溢出）。
 * 返回消耗的字节数（不含压缩指针跳转），出错返回 -1 */
static int decompress_name(const unsigned char *buf, int blen, int start,
                           char *out, int outsz) {
    int pos = start;
    int outlen = 0;
    int jumps = 0;
    int orig_pos = -1; /* 保存原始位置，跳转后恢复 */

    while (pos < blen) {
        int len = buf[pos];
        if (len == 0) {
            pos++;
            break;
        }
        if ((len & 0xc0) == 0xc0) {
            /* 压缩指针 */
            if (pos + 1 >= blen) return -1;
            if (orig_pos < 0) orig_pos = pos + 2; /* 保存返回位置 */
            pos = ((len & 0x3f) << 8) | buf[pos + 1];
            if (++jumps > 16) return -1; /* 防循环指针 */
            continue;
        }
        if (pos + 1 + len > blen) return -1;
        if (outlen > 0 && outlen < outsz) out[outlen++] = '.';
        if (outlen + len >= outsz) return -1; /* 防溢出 */
        memcpy(out + outlen, buf + pos + 1, len);
        outlen += len;
        pos += len + 1;
    }
    if (outlen >= outsz) return -1;
    out[outlen] = '\0';

    /* 返回原始消耗字节数 */
    return (orig_pos >= 0) ? orig_pos : (pos - start);
}

/* 解析 DNS 应答包中的记录 */
static void print_records(const unsigned char *buf, int blen,
                          int debug) {
    /* DNS 头部: ID(2) flags(2) qdcount(2) ancount(2) nscount(2) arcount(2) */
    if (blen < 12) {
        fprintf(stderr, "nslookup: reply too short\n");
        return;
    }

    int ancount = (buf[6] << 8) | buf[7];
    int qdcount = (buf[4] << 8) | buf[5];
    int rcode = buf[3] & 0x0f;

    if (debug) {
        fprintf(stderr, ";; Got %d answers, rcode=%d\n", ancount, rcode);
    }

    if (rcode != RCODE_NOERROR) {
        const char *err = "unknown";
        switch (rcode) {
        case RCODE_FORMERR:  err = "Format error"; break;
        case RCODE_SERVFAIL: err = "Server failure"; break;
        case RCODE_NXDOMAIN: err = "Non-existent domain"; break;
        case RCODE_REFUSED:  err = "Refused"; break;
        }
        fprintf(stderr, "nslookup: server can't find: %s\n", err);
        return;
    }

    /* 跳过问题段 */
    int pos = 12;
    for (int i = 0; i < qdcount && pos < blen; i++) {
        /* 跳过 QNAME */
        while (pos < blen) {
            int len = buf[pos];
            if (len == 0) { pos++; break; }
            if ((len & 0xc0) == 0xc0) { pos += 2; break; }
            pos += len + 1;
        }
        pos += 4; /* QTYPE(2) + QCLASS(2) */
    }

    /* 解析回答段 */
    for (int i = 0; i < ancount && pos < blen; i++) {
        /* 跳过 NAME（可能含压缩指针） */
        while (pos < blen) {
            int len = buf[pos];
            if (len == 0) { pos++; break; }
            if ((len & 0xc0) == 0xc0) { pos += 2; break; }
            pos += len + 1;
        }

        if (pos + 10 > blen) break;
        int type = (buf[pos] << 8) | buf[pos + 1];
        int rdlen = (buf[pos + 8] << 8) | buf[pos + 9];
        pos += 10;

        if (pos + rdlen > blen) break;

        /* 根据 type 输出 */
        if (type == DNS_TYPE_A && rdlen == 4) {
            /* A 记录 */
            struct in_addr addr;
            memcpy(&addr, buf + pos, 4);
            char ipstr[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &addr, ipstr, sizeof(ipstr));
            printf("Address: %s\n", ipstr);
        } else if (type == DNS_TYPE_AAAA && rdlen == 16) {
            /* AAAA 记录 */
            struct in6_addr addr;
            memcpy(&addr, buf + pos, 16);
            char ipstr[INET6_ADDRSTRLEN];
            inet_ntop(AF_INET6, &addr, ipstr, sizeof(ipstr));
            printf("Address: %s\n", ipstr);
        } else if (type == DNS_TYPE_CNAME) {
            /* CNAME */
            char name[256];
            if (decompress_name(buf, blen, pos, name, sizeof(name)) >= 0)
                printf("canonical name = %s\n", name);
        } else if (type == DNS_TYPE_MX && rdlen >= 3) {
            /* MX: preference(2) + exchange */
            int pref = (buf[pos] << 8) | buf[pos + 1];
            char name[256];
            if (decompress_name(buf, blen, pos + 2, name, sizeof(name)) >= 0)
                printf("mail exchanger = %d %s\n", pref, name);
        } else if (type == DNS_TYPE_NS || type == DNS_TYPE_PTR) {
            /* NS / PTR: 域名 */
            char name[256];
            if (decompress_name(buf, blen, pos, name, sizeof(name)) >= 0) {
                if (type == DNS_TYPE_NS)
                    printf("nameserver = %s\n", name);
                else
                    printf("name = %s\n", name);
            }
        } else if (type == DNS_TYPE_TXT) {
            /* TXT: 长度+字符串 */
            if (pos < blen) {
                int txtlen = buf[pos];
                if (pos + 1 + txtlen <= blen) {
                    char *txt = malloc(txtlen + 1);
                    if (txt) {
                        memcpy(txt, buf + pos + 1, txtlen);
                        txt[txtlen] = '\0';
                        printf("text = \"%s\"\n", txt);
                        free(txt);
                    }
                }
            }
        } else if (type == DNS_TYPE_SOA && rdlen >= 20) {
            /* SOA: MNAME RNAME SERIAL REFRESH RETRY EXPIRE MINIMUM */
            char mname[256];
            int consumed = decompress_name(buf, blen, pos, mname, sizeof(mname));
            if (consumed >= 0) {
                printf("origin = %s\n", mname);
            }
        } else {
            printf("(type %s, rdlen %d)\n", type_to_name(type), rdlen);
        }

        pos += rdlen;
    }
}

/* 从 /etc/resolv.conf 获取第一个 nameserver */
static int get_default_dns(struct sockaddr_in *sa) {
    FILE *f = fopen("/etc/resolv.conf", "r");
    if (!f) return -1;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char ns[64];
        if (sscanf(line, "nameserver %63s", ns) == 1) {
            struct in_addr addr;
            if (inet_aton(ns, &addr)) {
                memset(sa, 0, sizeof(*sa));
                sa->sin_family = AF_INET;
                sa->sin_port = htons(53);
                sa->sin_addr = addr;
                fclose(f);
                return 0;
            }
        }
    }
    fclose(f);
    return -1;
}

/* 发送 DNS 查询并解析回复 */
static int do_query(const char *host, int query_type,
                    const char *server, int timeout_sec, int debug) {
    unsigned char query[512];
    unsigned char reply[4096];
    int qlen = 0;
    int rlen = -1;

    /* 构造 DNS 查询报文 */
    unsigned char *p = query;

    /* 头部 */
    unsigned short id = (unsigned short)(time(NULL) & 0xffff);
    *p++ = id >> 8; *p++ = id & 0xff;
    /* flags: RD=1 (recursion desired) */
    *p++ = 0x01; *p++ = 0x00;
    /* qdcount=1 */
    *p++ = 0; *p++ = 1;
    /* ancount=nscount=arcount=0 */
    *p++ = 0; *p++ = 0;
    *p++ = 0; *p++ = 0;
    *p++ = 0; *p++ = 0;

    /* QNAME: host -> label 序列 */
    const char *dot = host;
    while (*dot) {
        const char *next = strchr(dot, '.');
        if (!next) next = dot + strlen(dot);
        int labellen = (int)(next - dot);
        if (labellen > 63 || labellen <= 0) {
            fprintf(stderr, "nslookup: invalid label in %s\n", host);
            return 1;
        }
        *p++ = (unsigned char)labellen;
        memcpy(p, dot, labellen);
        p += labellen;
        dot = *next ? next + 1 : next;
        if (!*next) break;
    }
    *p++ = 0; /* 根标签 */

    /* QTYPE + QCLASS */
    *p++ = (query_type >> 8) & 0xff;
    *p++ = query_type & 0xff;
    *p++ = 0; *p++ = DNS_CLASS_IN; /* IN */

    qlen = (int)(p - query);

    if (debug) {
        fprintf(stderr, ";; Sending %d-byte query for %s type %s\n",
                qlen, host, type_to_name(query_type));
    }

    /* 发送查询 */
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("nslookup: socket"); return 1; }

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(53);

    if (server) {
        /* 使用自定义服务器 */
        struct in_addr addr;
        if (inet_aton(server, &addr)) {
            sa.sin_addr = addr;
        } else {
            /* 尝试解析主机名 */
            struct addrinfo *res;
            struct addrinfo hints;
            memset(&hints, 0, sizeof(hints));
            hints.ai_family = AF_INET;
            if (getaddrinfo(server, "53", &hints, &res) == 0 && res) {
                memcpy(&sa, res->ai_addr, sizeof(sa));
                freeaddrinfo(res);
            } else {
                fprintf(stderr, "nslookup: cannot resolve server %s\n", server);
                close(sock);
                return 1;
            }
        }
    } else {
        /* 从 /etc/resolv.conf 获取默认 DNS */
        if (get_default_dns(&sa) != 0) {
            fprintf(stderr, "nslookup: no nameserver in /etc/resolv.conf\n");
            close(sock);
            return 1;
        }
    }

    /* 超时 */
    struct timeval tv = { .tv_sec = timeout_sec, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (sendto(sock, query, qlen, 0, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        perror("nslookup: sendto");
        close(sock);
        return 1;
    }

    rlen = recv(sock, reply, sizeof(reply), 0);
    close(sock);

    if (rlen < 0) {
        fprintf(stderr, "nslookup: query timed out or failed\n");
        return 1;
    }

    print_records(reply, rlen, debug);
    return 0;
}

/* PTR 反查：IP 地址 -> in-addr.arpa 域名 */
static int build_ptr_name(const char *ip, char *out, int outsz) {
    struct in_addr addr;
    if (!inet_aton(ip, &addr)) return 0;
    unsigned char *b = (unsigned char *)&addr.s_addr;
    snprintf(out, outsz, "%d.%d.%d.%d.in-addr.arpa",
             b[3], b[2], b[1], b[0]);
    return 1;
}

int main(int argc, char **argv) {
    set_program_name(argv[0]);
    utils_classic_init(argc, argv);

    int query_type = DNS_TYPE_A;
    int timeout_sec = 5;
    int debug = 0;
    int argi = 1;

    while (argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0') {
        if (!strcmp(argv[argi], "--version")) { version(); }
        if (!strcmp(argv[argi], "--help")) { usage(); return 0; }
        if (!strcmp(argv[argi], "--classic")) { argi++; continue; }
        if (!strncmp(argv[argi], "-type=", 6)) {
            query_type = type_from_name(argv[argi] + 6);
            if (query_type < 0) {
                fprintf(stderr, "nslookup: unknown type %s\n", argv[argi] + 6);
                return 2;
            }
            argi++;
            continue;
        }
        if (!strncmp(argv[argi], "-timeout=", 9)) {
            timeout_sec = atoi(argv[argi] + 9);
            argi++;
            continue;
        }
        if (!strcmp(argv[argi], "-debug") || !strcmp(argv[argi], "-d")) {
            debug = 1;
            argi++;
            continue;
        }
        /* 兼容旧式 -type T 语法 */
        if (!strcmp(argv[argi], "-type") && argi + 1 < argc) {
            query_type = type_from_name(argv[argi + 1]);
            if (query_type < 0) {
                fprintf(stderr, "nslookup: unknown type %s\n", argv[argi + 1]);
                return 2;
            }
            argi += 2;
            continue;
        }
        fprintf(stderr, "nslookup: unknown option %s\n", argv[argi]);
        return 2;
    }

    if (argi >= argc) { usage(); return 2; }

    const char *host = argv[argi++];
    const char *server = (argi < argc) ? argv[argi] : NULL;

    /* 输出服务器信息 */
    if (server) {
        printf("Server: %s\nAddress: %s#53\n\n", server, server);
    } else {
        /* 读取 /etc/resolv.conf 的第一个 nameserver */
        FILE *f = fopen("/etc/resolv.conf", "r");
        if (f) {
            char line[256];
            while (fgets(line, sizeof(line), f)) {
                char ns[64];
                if (sscanf(line, "nameserver %63s", ns) == 1) {
                    printf("Server: %s\nAddress: %s#53\n\n", ns, ns);
                    break;
                }
            }
            fclose(f);
        }
    }

    /* PTR 反查：IP 地址 -> 域名 */
    char ptr_name[256];
    if (query_type == DNS_TYPE_A && build_ptr_name(host, ptr_name, sizeof(ptr_name))) {
        printf("Name: %s\n", host);
        return do_query(ptr_name, DNS_TYPE_PTR, server, timeout_sec, debug);
    }

    printf("Name: %s\n", host);
    return do_query(host, query_type, server, timeout_sec, debug);
}
