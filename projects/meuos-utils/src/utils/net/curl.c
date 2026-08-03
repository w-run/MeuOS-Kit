/* curl — 轻量 HTTP 客户端
 *
 * 用法：curl [options] URL
 *
 * 选项：
 *   -o <file>    输出到文件（默认 stdout）
 *   -O           从 URL 提取文件名输出
 *   -L           跟随重定向
 *   -H "Hdr: V"  追加请求头
 *   -X METHOD    自定义 HTTP 方法
 *   -d <data>    POST 数据
 *   -s           静默模式
 *   -v           详细模式
 *   -w <fmt>     输出完成后格式化（如 "%{http_code}"）
 *   -i           包含响应头
 *   -m <sec>     最大超时
 *
 * 支持 HTTP/1.1 GET/POST，不解析 HTTPS（需要 TLS 层）。
 * --classic: 最小输出
 */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>

#include "meuos/utils.h"

#define MAX_REDIRECTS 10

static void usage(void) {
    fprintf(stderr,
        "Usage: curl [options] URL\n"
        "  -o <file>  Write to file instead of stdout\n"
        "  -O         Use remote filename for output\n"
        "  -L         Follow redirects\n"
        "  -H <hdr>   Add header\n"
        "  -X <method> HTTP method (default GET)\n"
        "  -d <data>  POST data\n"
        "  -s         Silent mode\n"
        "  -v         Verbose\n"
        "  -w <fmt>   Format after completion\n"
        "  -i         Include headers in output\n"
        "  -m <sec>   Max timeout\n"
        "  --classic  Minimal output\n");
}

/* URL 解析结果 */
struct url {
    char scheme[8];    /* "http" */
    char host[256];
    int  port;        /* 默认 80 */
    char path[1024];  /* 含前导 / */
};

/* 解析 URL：http://host[:port]/path */
static int parse_url(const char *url, struct url *u) {
    memset(u, 0, sizeof(*u));
    const char *p = url;

    /* scheme:// */
    const char *scheme_end = strstr(p, "://");
    if (scheme_end) {
        size_t slen = scheme_end - p;
        if (slen >= sizeof(u->scheme)) return -1;
        memcpy(u->scheme, p, slen);
        u->scheme[slen] = '\0';
        p = scheme_end + 3;
    } else {
        /* 无 scheme，默认 http */
        strcpy(u->scheme, "http");
    }

    /* host[:port]/path */
    const char *slash = strchr(p, '/');
    const char *host_end = slash ? slash : (p + strlen(p));
    const char *colon = memchr(p, ':', host_end - p);
    if (colon) {
        size_t hlen = colon - p;
        if (hlen >= sizeof(u->host)) return -1;
        memcpy(u->host, p, hlen);
        u->host[hlen] = '\0';
        u->port = atoi(colon + 1);
    } else {
        size_t hlen = host_end - p;
        if (hlen >= sizeof(u->host)) return -1;
        memcpy(u->host, p, hlen);
        u->host[hlen] = '\0';
        u->port = 80;
    }
    if (u->port == 0) u->port = 80;

    if (slash) {
        if (strlen(slash) >= sizeof(u->path)) return -1;
        strcpy(u->path, slash);
    } else {
        strcpy(u->path, "/");
    }
    return 0;
}

/* 建立 TCP 连接 */
static int connect_host(const char *host, int port, int timeout_sec) {
    struct addrinfo hints, *res, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char portstr[8];
    snprintf(portstr, sizeof(portstr), "%d", port);

    int gai = getaddrinfo(host, portstr, &hints, &res);
    if (gai != 0) {
        fprintf(stderr, "curl: %s: %s\n", host, gai_strerror(gai));
        return -1;
    }

    int sock = -1;
    for (rp = res; rp; rp = rp->ai_next) {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock < 0) continue;

        if (timeout_sec > 0) {
            struct timeval tv = { .tv_sec = timeout_sec, .tv_usec = 0 };
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        }

        if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0)
            break;
        close(sock);
        sock = -1;
    }
    freeaddrinfo(res);
    return sock;
}

int main(int argc, char **argv) {
    set_program_name(argv[0]);
    utils_classic_init(argc, argv);

    const char *output_file = NULL;
    int use_remote_name = 0;
    int follow_redirects = 0;
    const char *method = NULL;
    const char *post_data = NULL;
    int silent = 0;
    int verbose = 0;
    int include_headers = 0;
    int max_timeout = 0;
    const char *write_fmt = NULL;

    /* 收集自定义头 */
    char *headers[32];
    int nheaders = 0;

    int argi = 1;
    while (argi < argc) {
        if (argv[argi][0] != '-' || argv[argi][1] == '\0')
            break;  /* URL 参数 */
        if (!strcmp(argv[argi], "--version")) { version(); }
        if (!strcmp(argv[argi], "--help")) { usage(); return 0; }
        if (!strcmp(argv[argi], "--classic")) { argi++; continue; }
        char *p = argv[argi] + 1;
        while (*p) {
            switch (*p) {
            case 'o': case 'X': case 'd': case 'w': case 'm': case 'H':
                if (argi + 1 >= argc) {
                    fprintf(stderr, "curl: -%c requires an argument\n", *p);
                    return 2;
                }
                if (*p == 'o') output_file = argv[++argi];
                else if (*p == 'X') method = argv[++argi];
                else if (*p == 'd') post_data = argv[++argi];
                else if (*p == 'w') write_fmt = argv[++argi];
                else if (*p == 'm') max_timeout = atoi(argv[++argi]);
                else if (*p == 'H') {
                    if (nheaders < 32)
                        headers[nheaders++] = argv[++argi];
                }
                p++;
                goto next_arg;
            case 'O': use_remote_name = 1; break;
            case 'L': follow_redirects = 1; break;
            case 's': silent = 1; break;
            case 'v': verbose = 1; break;
            case 'i': include_headers = 1; break;
            default:
                fprintf(stderr, "curl: unknown option -%c\n", *p);
                return 2;
            }
            p++;
        }
    next_arg:
        argi++;
    }

    if (argi >= argc) { usage(); return 2; }

    const char *url_str = argv[argi];
    struct url url;
    if (parse_url(url_str, &url) != 0) {
        fprintf(stderr, "curl: invalid URL: %s\n", url_str);
        return 2;
    }

    if (strcmp(url.scheme, "http") != 0) {
        fprintf(stderr, "curl: scheme '%s' not supported (only http)\n", url.scheme);
        return 2;
    }

    /* 重定向循环 */
    int redirect_count = 0;
    int http_code = 0;
    size_t total_bytes = 0;

    for (;;) {
        if (!silent || verbose)
            fprintf(stderr, "* Connecting to %s:%d ...\n", url.host, url.port);

        int sock = connect_host(url.host, url.port, max_timeout);
        if (sock < 0) {
            fprintf(stderr, "curl: failed to connect to %s:%d\n", url.host, url.port);
            return 7;
        }

        /* 构造 HTTP 请求 */
        const char *actual_method = method ? method :
            (post_data ? "POST" : "GET");

        char request[8192];
        int reqlen = 0;
        reqlen += snprintf(request + reqlen, sizeof(request) - reqlen,
            "%s %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: curl/meuos\r\n"
            "Accept: */*\r\n"
            "Connection: close\r\n",
            actual_method, url.path, url.host);

        /* POST data */
        if (post_data) {
            reqlen += snprintf(request + reqlen, sizeof(request) - reqlen,
                "Content-Type: application/x-www-form-urlencoded\r\n"
                "Content-Length: %zu\r\n",
                strlen(post_data));
        }

        /* 自定义头 */
        for (int i = 0; i < nheaders; i++) {
            reqlen += snprintf(request + reqlen, sizeof(request) - reqlen,
                "%s\r\n", headers[i]);
        }

        reqlen += snprintf(request + reqlen, sizeof(request) - reqlen, "\r\n");

        if (post_data) {
            size_t plen = strlen(post_data);
            if (reqlen + plen < sizeof(request)) {
                memcpy(request + reqlen, post_data, plen);
                reqlen += plen;
            }
        }

        if (verbose)
            fprintf(stderr, "* Request:\n%s\n", request);

        /* 发送请求 */
        int sent = 0;
        while (sent < reqlen) {
            int n = write(sock, request + sent, reqlen - sent);
            if (n < 0) {
                fprintf(stderr, "curl: write error: %s\n", strerror(errno));
                close(sock);
                return 7;
            }
            sent += n;
        }

        /* 读取响应 */
        FILE *out = stdout;
        char *filename = NULL;

        if (output_file) {
            out = fopen(output_file, "wb");
            if (!out) {
                fprintf(stderr, "curl: cannot open %s: %s\n",
                        output_file, strerror(errno));
                close(sock);
                return 2;
            }
        } else if (use_remote_name) {
            /* 从路径提取文件名 */
            const char *base = strrchr(url.path, '/');
            filename = xstrdup(base ? base + 1 : "index.html");
            if (!filename[0]) { free(filename); filename = xstrdup("index.html"); }
            out = fopen(filename, "wb");
            if (!out) {
                fprintf(stderr, "curl: cannot open %s: %s\n",
                        filename, strerror(errno));
                free(filename);
                close(sock);
                return 2;
            }
        }

        char buf[8192];
        int header_done = 0;
        int header_started = 0;
        char status_line[256] = "";

        int n;
        while ((n = read(sock, buf, sizeof(buf))) > 0) {
            const char *p = buf;
            int remaining = n;

            if (!header_done) {
                /* 解析状态行和头部 */
                while (remaining > 0) {
                    /* 找行尾 */
                    const char *eol = memchr(p, '\n', remaining);
                    if (!eol) {
                        /* 不完整的行，暂不处理（简化） */
                        break;
                    }
                    int linelen = eol - p;
                    /* 去掉 \r */
                    if (linelen > 0 && p[linelen - 1] == '\r')
                        linelen--;

                    if (!header_started) {
                        /* 状态行 */
                        if (linelen < (int)sizeof(status_line)) {
                            memcpy(status_line, p, linelen);
                            status_line[linelen] = '\0';
                        }
                        header_started = 1;
                        /* 解析 HTTP code */
                        if (sscanf(status_line, "HTTP/%*d.%*d %d", &http_code) != 1)
                            http_code = 0;

                        if (include_headers)
                            fprintf(out, "%.*s\n", linelen, p);
                    } else if (linelen == 0) {
                        /* 头部结束 */
                        header_done = 1;
                        remaining -= (eol - p) + 1;
                        p = eol + 1;
                        break;
                    } else {
                        /* 检查 Location 头 */
                        if (follow_redirects && linelen > 9 &&
                            strncasecmp(p, "location:", 9) == 0) {
                            const char *loc = p + 9;
                            while (*loc == ' ' || *loc == '\t') loc++;
                            char new_url[1024];
                            int ll = linelen - (loc - p);
                            if (ll < (int)sizeof(new_url)) {
                                memcpy(new_url, loc, ll);
                                new_url[ll] = '\0';
                                /* 保存重定向 URL */
                                if (parse_url(new_url, &url) == 0)
                                    redirect_count++;
                            }
                        }
                        if (include_headers)
                            fprintf(out, "%.*s\n", linelen, p);
                    }

                    remaining -= (eol - p) + 1;
                    p = eol + 1;
                }
            }

            if (header_done && remaining > 0) {
                fwrite(p, 1, remaining, out);
                total_bytes += remaining;
            }
        }

        close(sock);
        if (out != stdout) fclose(out);
        if (filename) free(filename);

        /* 检查重定向 */
        if (follow_redirects && (http_code == 301 || http_code == 302 ||
            http_code == 303 || http_code == 307 || http_code == 308) &&
            redirect_count > 0 && redirect_count <= MAX_REDIRECTS) {
            if (!silent)
                fprintf(stderr, "* Redirect to %s://%s:%d%s\n",
                    url.scheme, url.host, url.port, url.path);
            continue;
        }

        break;
    }

    /* -w 格式化输出 */
    if (write_fmt) {
        for (const char *p = write_fmt; *p; p++) {
            if (*p == '%' && p[1] == '{' ) {
                const char *end = strstr(p + 2, "}");
                if (end) {
                    int len = end - (p + 2);
                    if (len == 9 && strncmp(p + 2, "http_code", 9) == 0)
                        printf("%d", http_code);
                    else if (len == 11 && strncmp(p + 2, "size_download", 13) == 0)
                        printf("%zu", total_bytes);
                    p = end;
                }
            } else {
                putchar(*p);
            }
        }
    }

    return (http_code >= 200 && http_code < 400) ? 0 : 22;
}
