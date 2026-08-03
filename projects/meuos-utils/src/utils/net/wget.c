/* wget — HTTP 文件下载器
 *
 * 用法：wget [options] URL...
 *
 * 选项：
 *   -O <file>    输出文件名
 *   -q           静默模式
 *   -v           详细模式
 *   -c           断点续传（Range 请求）
 *   -S           显示服务器响应头
 *   -t <n>       最大重试次数（默认 20）
 *   -T <sec>     超时秒数
 *   -U <agent>   User-Agent 字符串
 *   -P <dir>     保存到目录
 *
 * 默认：下载到当前目录，文件名从 URL 提取
 *
 * --classic: 传统 wget 输出
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
#include <sys/stat.h>

#include "meuos/utils.h"

static void usage(void) {
    fprintf(stderr,
        "Usage: wget [options] URL...\n"
        "  -O <file>  Output filename\n"
        "  -q         Quiet\n"
        "  -v         Verbose\n"
        "  -c         Continue (resume download)\n"
        "  -S         Show server response headers\n"
        "  -t <n>     Max retries (default 20)\n"
        "  -T <sec>   Timeout in seconds\n"
        "  -U <agent> User-Agent string\n"
        "  -P <dir>   Save to directory\n"
        "  --classic  Traditional format\n");
}

struct url {
    char host[256];
    int  port;
    char path[2048];
};

static int parse_url(const char *url, struct url *u) {
    memset(u, 0, sizeof(*u));
    const char *p = strstr(url, "://");
    if (p) p += 3;
    else p = url;

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

static int connect_host(const char *host, int port, int timeout_sec) {
    struct addrinfo hints, *res, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char portstr[8];
    snprintf(portstr, sizeof(portstr), "%d", port);

    int gai = getaddrinfo(host, portstr, &hints, &res);
    if (gai != 0) return -1;

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

static char *extract_filename(const char *path) {
    const char *base = strrchr(path, '/');
    if (base && base[1]) return xstrdup(base + 1);
    return xstrdup("index.html");
}

static int download(const char *url_str, const char *output_file,
                    const char *save_dir, int quiet, int verbose,
                    int resume, int show_headers, int timeout,
                    const char *user_agent, int max_retries) {
    struct url url;
    if (parse_url(url_str, &url) != 0) {
        fprintf(stderr, "wget: invalid URL: %s\n", url_str);
        return 1;
    }

    /* 确定输出文件名 */
    char *local_name = NULL;
    if (output_file)
        local_name = xstrdup(output_file);
    else
        local_name = extract_filename(url.path);

    /* 确定输出路径 */
    char filepath[1024];
    if (save_dir) {
        /* 确保 save_dir 存在 */
        mkdir(save_dir, 0755);
        snprintf(filepath, sizeof(filepath), "%s/%s", save_dir, local_name);
    } else {
        snprintf(filepath, sizeof(filepath), "%s", local_name);
    }
    free(local_name);

    /* 断点续传：检查已有文件大小 */
    long existing_size = 0;
    FILE *out;
    if (resume) {
        struct stat st;
        if (stat(filepath, &st) == 0) {
            existing_size = st.st_size;
            out = fopen(filepath, "ab");
        } else {
            out = fopen(filepath, "wb");
        }
    } else {
        out = fopen(filepath, "wb");
    }
    if (!out) {
        fprintf(stderr, "wget: cannot write %s: %s\n", filepath, strerror(errno));
        return 1;
    }

    int attempt = 0;

    while (attempt < max_retries) {
        if (!quiet)
            fprintf(stderr, "--%s %s:%d%s\n",
                    attempt == 0 ? "Trying" : "Retrying",
                    url.host, url.port, url.path);

        int sock = connect_host(url.host, url.port, timeout);
        if (sock < 0) {
            fprintf(stderr, "wget: connect failed for %s:%d\n", url.host, url.port);
            attempt++;
            continue;
        }

        /* 构造请求 */
        char req[4096];
        int reqlen = 0;
        reqlen += snprintf(req + reqlen, sizeof(req) - reqlen,
            "GET %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: %s\r\n"
            "Accept: */*\r\n"
            "Connection: close\r\n",
            url.path, url.host, user_agent ? user_agent : "Wget/meuos");

        if (resume && existing_size > 0) {
            reqlen += snprintf(req + reqlen, sizeof(req) - reqlen,
                "Range: bytes=%ld-\r\n", existing_size);
        }

        reqlen += snprintf(req + reqlen, sizeof(req) - reqlen, "\r\n");

        /* 发送请求 */
        int sent = 0;
        while (sent < reqlen) {
            int n = write(sock, req + sent, reqlen - sent);
            if (n < 0) break;
            sent += n;
        }

        /* 读取响应 */
        char buf[8192];
        int header_done = 0;
        int header_started = 0;
        int http_code = 0;
        long content_length = -1;
        size_t downloaded = 0;

        int n;
        while ((n = read(sock, buf, sizeof(buf))) > 0) {
            const char *p = buf;
            int remaining = n;

            if (!header_done) {
                while (remaining > 0) {
                    const char *eol = memchr(p, '\n', remaining);
                    if (!eol) break;
                    int linelen = eol - p;
                    if (linelen > 0 && p[linelen - 1] == '\r')
                        linelen--;

                    if (!header_started) {
                        /* 状态行 */
                        sscanf(p, "HTTP/%*d.%*d %d", &http_code);
                        header_started = 1;
                        if (show_headers)
                            printf("%.*s\n", linelen, p);
                    } else if (linelen == 0) {
                        header_done = 1;
                        remaining -= (eol - p) + 1;
                        p = eol + 1;
                        if (!quiet)
                            fprintf(stderr, "HTTP code %d\n", http_code);
                        break;
                    } else {
                        /* Content-Length */
                        if (strncasecmp(p, "Content-Length:", 15) == 0) {
                            content_length = atol(p + 15);
                        }
                        if (show_headers)
                            printf("%.*s\n", linelen, p);
                    }
                    remaining -= (eol - p) + 1;
                    p = eol + 1;
                }
            }

            if (header_done && remaining > 0) {
                fwrite(p, 1, remaining, out);
                downloaded += remaining;
                if (!quiet && verbose && content_length > 0) {
                    long pct = (long)(downloaded * 100 / content_length);
                    fprintf(stderr, "\r  Downloading: %3ld%% (%zu/%ld bytes)",
                            pct, downloaded, content_length);
                }
            }
        }

        close(sock);

        if (!quiet && verbose)
            fprintf(stderr, "\n");

        if (http_code >= 200 && http_code < 300) {
            fclose(out);
            if (!quiet)
                fprintf(stderr, "Saved to %s (%zu bytes)\n", filepath,
                        downloaded + existing_size);
            return 0;
        } else if (http_code == 416) {
            /* Range not satisfiable */
            if (!quiet)
                fprintf(stderr, "File already complete: %s\n", filepath);
            fclose(out);
            return 0;
        } else if (http_code >= 500) {
            /* 服务器错误，重试 */
            attempt++;
            if (!quiet)
                fprintf(stderr, "Server error %d, retrying (%d/%d)\n",
                        http_code, attempt, max_retries);
        } else {
            /* 其他错误，不重试 */
            fprintf(stderr, "wget: HTTP error %d for %s\n", http_code, url_str);
            fclose(out);
            return 1;
        }
    }

    fclose(out);
    fprintf(stderr, "wget: giving up after %d attempts\n", max_retries);
    return 1;
}

int main(int argc, char **argv) {
    set_program_name(argv[0]);
    utils_classic_init(argc, argv);

    const char *output_file = NULL;
    const char *save_dir = NULL;
    const char *user_agent = NULL;
    int quiet = 0, verbose = 0, resume = 0;
    int show_headers = 0, timeout = 0;
    int max_retries = 20;
    int argi = 1;

    while (argi < argc) {
        if (argv[argi][0] != '-' || argv[argi][1] == '\0')
            break;
        if (!strcmp(argv[argi], "--version")) { version(); }
        if (!strcmp(argv[argi], "--help")) { usage(); return 0; }
        if (!strcmp(argv[argi], "--classic")) { argi++; continue; }
        char *p = argv[argi] + 1;
        while (*p) {
            switch (*p) {
            case 'O': case 'P': case 't': case 'T': case 'U':
                if (argi + 1 >= argc) {
                    fprintf(stderr, "wget: -%c requires an argument\n", *p);
                    return 2;
                }
                switch (*p) {
                case 'O': output_file = argv[++argi]; break;
                case 'P': save_dir = argv[++argi]; break;
                case 't': max_retries = atoi(argv[++argi]); break;
                case 'T': timeout = atoi(argv[++argi]); break;
                case 'U': user_agent = argv[++argi]; break;
                }
                p++;
                goto next_arg;
            case 'q': quiet = 1; break;
            case 'v': verbose = 1; break;
            case 'c': resume = 1; break;
            case 'S': show_headers = 1; break;
            default:
                fprintf(stderr, "wget: unknown option -%c\n", *p);
                return 2;
            }
            p++;
        }
    next_arg:
        argi++;
    }

    if (argi >= argc) { usage(); return 2; }

    int rc = 0;
    for (int i = argi; i < argc; i++) {
        if (download(argv[i], output_file, save_dir, quiet, verbose,
                     resume, show_headers, timeout, user_agent,
                     max_retries) != 0)
            rc = 1;
        /* 仅第一个 URL 使用 -O */
        if (output_file && i + 1 < argc)
            output_file = NULL;
    }
    return rc;
}
