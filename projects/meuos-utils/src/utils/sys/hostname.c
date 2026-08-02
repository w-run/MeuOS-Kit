/* hostname — 显示或设置主机名
 * 用法：hostname [-s] [NAME]
 * 选项：-s 短名（不显示域名）, -i IP 地址, -f FQDN
 */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "meuos/utils.h"

int main(int argc, char **argv) {
    int argi = utils_init(argc, argv);
    if (argi < argc && !strcmp(argv[argi], "--help")) utils_usage("Usage: hostname [-s] [-i] [NAME]\n");
    int short_name = 0, ip = 0;
    while (argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0') {
        for (char *p = argv[argi]+1; *p; p++) {
            if (*p == 's') short_name = 1;
            else if (*p == 'i') ip = 1;
            else if (*p == 'f') {} /* FQDN = default */
            else { fprintf(stderr, "hostname: unknown option -%c\n", *p); return 2; }
        }
        argi++;
    }
    if (argi < argc) {
        /* 设置主机名 */
        if (sethostname(argv[argi], strlen(argv[argi])) < 0) {
            fprintf(stderr, "hostname: %s: %s\n", argv[argi], strerror(errno));
            return 1;
        }
        return 0;
    }
    char buf[256];
    if (gethostname(buf, sizeof(buf)) < 0) {
        fprintf(stderr, "hostname: %s\n", strerror(errno));
        return 1;
    }
    if (ip) {
        struct hostent *he = gethostbyname(buf);
        if (he && he->h_addr_list[0]) {
            struct in_addr addr;
            memcpy(&addr, he->h_addr_list[0], sizeof(addr));
            printf("%s\n", inet_ntoa(addr));
            return 0;
        }
        fprintf(stderr, "hostname: cannot resolve\n");
        return 1;
    }
    if (short_name) {
        char *dot = strchr(buf, '.');
        if (dot) *dot = '\0';
    }
    puts(buf);
    return 0;
}
