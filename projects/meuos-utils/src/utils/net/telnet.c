/* telnet — 远程终端客户端
 *
 * 用法：telnet [-l user] [-p port] host [port]
 *
 * 连接到远程主机并建立交互式终端会话。
 * 支持：
 *   - Telnet 协议 IAC 命令处理 (RFC 854)
 *   - 基本 WILL/WONT/DO/DONT 选项协商
 *   - Terminal Type (TTYPE) 子协商：回复 "xterm"
 *   - NAWS (窗口大小) 子协商：发送当前终端尺寸
 *   - Ctrl+] 进入命令模式 (quit/close/status/help)
 *   - 终端 raw 模式，SIGINT/SIGPIPE 安全恢复
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
#include <signal.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <termios.h>

#include "meuos/utils.h"

/* Telnet IAC commands (RFC 854) */
#define IAC   255
#define DONT  254
#define DO    253
#define WONT  252
#define WILL  251
#define SB    250
#define GA    249
#define EL    248
#define EC    247
#define AYT   246
#define AO    245
#define IP    244
#define BRK   243
#define DM    242
#define NOP   241
#define SE    240

/* Telnet options */
#define OPT_BINARY  0
#define OPT_ECHO    1
#define OPT_SGA     3   /* Suppress Go Ahead */
#define OPT_STATUS  5
#define OPT_TM      6   /* Timing Mark */
#define OPT_TTYPE   24  /* Terminal Type */
#define OPT_NAWS    31  /* Negotiate About Window Size */
#define OPT_TSPEED  32
#define OPT_LINEMODE 34

/* 我们已同意的选项（用于协商状态追踪） */
static int we_will_ttype = 0;
static int we_will_naws = 0;

static void usage(void) {
    fprintf(stderr,
        "Usage: telnet [-l user] [-p port] host [port]\n"
        "  -l user   Login as user (sends to remote)\n"
        "  -p port   Port number (default 23)\n"
        "  host      Remote host to connect to\n"
        "  port      Port (alternative position)\n"
        "  --classic Traditional format\n"
        "\n"
        "Escape character is Ctrl+] (0x1d)\n"
        "Commands in escape mode: quit, close, status, help\n");
}

static struct termios orig_termios;
static int term_saved = 0;

static void term_raw_on(void) {
    if (!term_saved) {
        tcgetattr(STDIN_FILENO, &orig_termios);
        term_saved = 1;
    }
    struct termios raw = orig_termios;
    cfmakeraw(&raw);
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
}

static void term_restore(void) {
    if (term_saved)
        tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
}

/* SIGINT 处理：恢复终端后退出 */
static void sigint_handler(int sig) {
    (void)sig;
    term_restore();
    _exit(130); /* 128 + SIGINT(2) */
}

/* 发送 IAC 协商 */
static void send_iac(int sock, int cmd, int opt) {
    unsigned char buf[3];
    buf[0] = IAC;
    buf[1] = (unsigned char)cmd;
    buf[2] = (unsigned char)opt;
    /* 忽略 write 错误（连接可能已断开，主循环会检测到） */
    ssize_t r = write(sock, buf, 3);
    (void)r;
}

/* 发送 NAWS 子协商：终端窗口尺寸 */
static void send_naws(int sock) {
    struct winsize ws;
    int rows = 24, cols = 80;
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0) {
        rows = ws.ws_row ? ws.ws_row : 24;
        cols = ws.ws_col ? ws.ws_col : 80;
    }
    unsigned char buf[9];
    buf[0] = IAC;
    buf[1] = SB;
    buf[2] = OPT_NAWS;
    buf[3] = (cols >> 8) & 0xff;
    buf[4] = cols & 0xff;
    buf[5] = (rows >> 8) & 0xff;
    buf[6] = rows & 0xff;
    buf[7] = IAC;
    buf[8] = SE;
    ssize_t r = write(sock, buf, 9);
    (void)r;
}

/* 发送 TTYPE 子协商：终端类型 */
static void send_ttype(int sock) {
    const char *term = getenv("TERM");
    if (!term) term = "xterm";
    size_t tlen = strlen(term);
    if (tlen > 40) tlen = 40; /* 安全截断 */
    unsigned char buf[6 + 40];
    buf[0] = IAC;
    buf[1] = SB;
    buf[2] = OPT_TTYPE;
    buf[3] = 0; /* IS */
    memcpy(buf + 4, term, tlen);
    buf[4 + tlen] = IAC;
    buf[5 + tlen] = SE;
    ssize_t r = write(sock, buf, 6 + tlen);
    (void)r;
}

/* 处理来自服务器的 IAC 序列 */
static int handle_iac(int sock, const unsigned char *buf, int *pos, int len) {
    if (*pos + 1 >= len || buf[*pos] != IAC)
        return 0;

    int cmd = buf[*pos + 1];
    *pos += 2;

    if (cmd == IAC) {
        /* IAC IAC = literal 0xFF */
        return 0xFF;
    }

    if (cmd == DO || cmd == DONT || cmd == WILL || cmd == WONT) {
        if (*pos >= len) return -1; /* 不完整 */
        int opt = buf[*pos];
        (*pos)++;

        /* 简化协商策略：
         * - DO/WILL: 对大多数选项回 WONT/DONT（拒绝）
         * - 但对 SGA (3) 和 ECHO (1) 回 WILL/DO（接受）
         * - 对 TTYPE (24) 回 WILL（接受），之后处理子协商
         * - 对 NAWS (31) 回 WILL（接受），之后处理子协商
         */
        if (cmd == DO) {
            if (opt == OPT_SGA) {
                send_iac(sock, WILL, opt);
            } else if (opt == OPT_TTYPE) {
                we_will_ttype = 1;
                send_iac(sock, WILL, opt);
            } else if (opt == OPT_NAWS) {
                we_will_naws = 1;
                send_iac(sock, WILL, opt);
                send_naws(sock);
            } else {
                send_iac(sock, WONT, opt);
            }
        } else if (cmd == WILL) {
            if (opt == OPT_ECHO || opt == OPT_SGA) {
                send_iac(sock, DO, opt);
            } else {
                send_iac(sock, DONT, opt);
            }
        } else if (cmd == DONT) {
            if (opt == OPT_TTYPE) we_will_ttype = 0;
            if (opt == OPT_NAWS) we_will_naws = 0;
            send_iac(sock, WONT, opt);
        } else if (cmd == WONT) {
            send_iac(sock, DONT, opt);
        }
        return -2; /* 已处理 */
    }

    if (cmd == SB) {
        /* 子协商：解析到 SE */
        int sb_opt = -1;
        if (*pos < len) sb_opt = buf[*pos];

        /* 查找 SE */
        while (*pos < len) {
            if (buf[*pos] == IAC && *pos + 1 < len && buf[*pos + 1] == SE) {
                *pos += 2;
                break;
            }
            (*pos)++;
        }

        /* 处理特定子协商 */
        if (sb_opt == OPT_TTYPE && we_will_ttype) {
            /* SB TTYPE SEND IAC SE -> 回复终端类型 */
            send_ttype(sock);
        }
        return -2;
    }

    if (cmd == GA || cmd == NOP) {
        return -2; /* 忽略 */
    }

    if (cmd == BRK || cmd == IP) {
        return -2; /* 中断信号，忽略 */
    }

    /* 其他命令忽略 */
    return -2;
}

/* 命令模式：用户按 Ctrl+] 后进入 */
static int command_mode(int sock) {
    (void)sock;
    char buf[256];
    while (1) {
        fprintf(stderr, "telnet> ");
        fflush(stderr);
        if (!fgets(buf, sizeof(buf), stdin)) return 0;
        /* 去掉换行 */
        size_t L = strlen(buf);
        if (L && buf[L-1] == '\n') buf[--L] = '\0';

        if (!strcmp(buf, "quit") || !strcmp(buf, "q") ||
            !strcmp(buf, "close") || !strcmp(buf, "c")) {
            return 1; /* 关闭连接 */
        }
        if (!strcmp(buf, "status") || !strcmp(buf, "st")) {
            printf("Connected to remote host.\n");
            printf("Escape character is Ctrl+] (0x1d)\n");
            continue;
        }
        if (!strcmp(buf, "help") || !strcmp(buf, "h") || !strcmp(buf, "?")) {
            printf("Commands:\n"
                   "  quit   Close connection and exit\n"
                   "  close  Same as quit\n"
                   "  status Show connection status\n"
                   "  help   Show this help\n"
                   "  <CR>   Return to session\n");
            continue;
        }
        if (buf[0] == '\0') {
            return 0; /* 空行：返回会话 */
        }
        printf("Unknown command: %s (try 'help')\n", buf);
    }
}

int main(int argc, char **argv) {
    set_program_name(argv[0]);
    utils_classic_init(argc, argv);

    const char *login_user = NULL;
    const char *port = "23";
    int argi = 1;

    while (argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0') {
        if (!strcmp(argv[argi], "--version")) { version(); }
        if (!strcmp(argv[argi], "--help")) { usage(); return 0; }
        if (!strcmp(argv[argi], "--classic")) { argi++; continue; }
        char *p = argv[argi] + 1;
        while (*p) {
            switch (*p) {
            case 'l':
                if (argi + 1 >= argc) { fprintf(stderr, "telnet: -l requires argument\n"); return 2; }
                login_user = argv[++argi];
                p++;
                goto next_arg;
            case 'p':
                if (argi + 1 >= argc) { fprintf(stderr, "telnet: -p requires argument\n"); return 2; }
                port = argv[++argi];
                p++;
                goto next_arg;
            default:
                fprintf(stderr, "telnet: unknown option -%c\n", *p);
                return 2;
            }
        }
    next_arg:
        argi++;
    }

    if (argi >= argc) { usage(); return 2; }

    const char *host = argv[argi++];
    if (argi < argc) port = argv[argi]; /* port 作为位置参数 */

    /* 解析地址 */
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;  /* 支持 IPv4 和 IPv6 */
    hints.ai_socktype = SOCK_STREAM;

    int gai = getaddrinfo(host, port, &hints, &res);
    if (gai != 0) {
        fprintf(stderr, "telnet: %s: %s\n", host, gai_strerror(gai));
        return 2;
    }

    int sock = -1;
    struct addrinfo *rp;
    for (rp = res; rp; rp = rp->ai_next) {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock < 0) continue;
        if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(sock);
        sock = -1;
    }
    freeaddrinfo(res);

    if (sock < 0) {
        fprintf(stderr, "telnet: connect to %s:%s: %s\n", host, port, strerror(errno));
        return 1;
    }

    /* 输出连接信息 */
    fprintf(stderr, "Connected to %s:%s.\n", host, port);
    fprintf(stderr, "Escape character is Ctrl+] (0x1d).\n");

    /* 如果指定了 -l user，发送用户名（有些服务器在连接后提示 login:） */
    (void)login_user; /* 暂不自动发送，留给用户手动输入 */

    /* 发送初始协商 */
    send_iac(sock, DO, OPT_SGA);
    send_iac(sock, WILL, OPT_SGA);
    send_iac(sock, WILL, OPT_NAWS);

    /* 进入 raw 模式 */
    term_raw_on();
    atexit(term_restore);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, sigint_handler);

    /* 主循环：select stdin + socket */
    int running = 1;
    while (running) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        FD_SET(sock, &fds);
        int maxfd = sock;

        int n = select(maxfd + 1, &fds, NULL, NULL, NULL);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }

        /* 服务器数据 */
        if (FD_ISSET(sock, &fds)) {
            unsigned char buf[4096];
            int r = read(sock, buf, sizeof(buf));
            if (r <= 0) {
                fprintf(stderr, "\nConnection closed by foreign host.\n");
                running = 0;
                break;
            }

            /* 处理 IAC 命令 */
            int i = 0;
            while (i < r) {
                if (buf[i] == IAC && i + 1 < r) {
                    int save = i;
                    int result = handle_iac(sock, buf, &i, r);
                    if (result == -2) {
                        /* IAC 已处理，继续 */
                        continue;
                    } else if (result == 0xFF) {
                        /* IAC IAC = literal 0xFF */
                        putchar(0xFF);
                        continue;
                    } else if (result == -1) {
                        /* 不完整序列，跳过 */
                        i = save;
                        break;
                    }
                }
                /* 普通数据 */
                putchar(buf[i]);
                i++;
            }
            fflush(stdout);
        }

        /* 用户输入 */
        if (FD_ISSET(STDIN_FILENO, &fds)) {
            unsigned char buf[4096];
            int r = read(STDIN_FILENO, buf, sizeof(buf));
            if (r <= 0) break;

            int out = 0;
            for (int i = 0; i < r; i++) {
                if (buf[i] == 0x1d) {
                    /* Ctrl+] = 进入命令模式 */
                    term_restore();
                    if (command_mode(sock)) {
                        running = 0;
                        break;
                    }
                    term_raw_on();
                    continue;
                }
                buf[out++] = buf[i];
            }
            if (out > 0 && running) {
                ssize_t wr = write(sock, buf, out);
                (void)wr;
            }
        }
    }

    term_restore();
    close(sock);
    fprintf(stderr, "Connection to %s closed.\n", host);
    return 0;
}
