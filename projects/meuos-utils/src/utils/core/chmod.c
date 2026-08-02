/* chmod - 修改文件权限
 *
 * 支持选项：
 *   -R, --recursive  递归
 *   -c, --changes    报告每个改动
 *   -f, --silent     静默错误
 *   --classic        POSIX 风格
 *   --help / --version
 *
 * MODE 支持两种形式：
 *   1. 八进制：如 755 / 0644
 *   2. 符号：[ugoa]*[+-=][rwxst]* （可叠加，逗号分隔，如 u+x,go-w）
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "meuos/utils.h"

static int flag_recursive = 0;
static int flag_changes = 0;
static int flag_silent = 0;

static mode_t parse_octal(const char *s) {
    if (!s || !*s) return (mode_t)-1;
    char *end;
    long m = strtol(s, &end, 8);
    if (*end != '\0' || m < 0 || m > 07777) return (mode_t)-1;
    return (mode_t)m;
}

/* 解析符号模式：[ugoa]*[+-=][rwxst]*
 * who: u=owner, g=group, o=other, a=all
 * op: +-= 
 * perm: rwxst (s = setuid/setgid, t = sticky) */
static int apply_symbolic(mode_t *mode, const char *spec) {
    const char *p = spec;
    while (*p) {
        /* who */
        int who = 0;  /* 位掩码：1=u, 2=g, 4=o, 8=a */
        if (*p == 'a' || (!strchr("ugoa", *p))) {
            who = 1 | 2 | 4;
            if (*p == 'a') p++;
        }
        while (*p && strchr("ugoa", *p)) {
            switch (*p) {
            case 'u': who |= 1; break;
            case 'g': who |= 2; break;
            case 'o': who |= 4; break;
            case 'a': who |= 1 | 2 | 4; break;
            }
            p++;
        }
        if (who == 0) who = 1 | 2 | 4;

        /* op */
        char op = *p;
        if (op != '+' && op != '-' && op != '=') {
            fprintf(stderr, "%s: invalid mode: '%s'\n", program_name, spec);
            return -1;
        }
        p++;

        /* perm 字符对应 owner 位的 rwx 值 */
        mode_t perm = 0;
        while (*p && strchr("rwxst", *p)) {
            switch (*p) {
            case 'r': perm |= 0400; break;
            case 'w': perm |= 0200; break;
            case 'x': perm |= 0100; break;
            case 's': perm |= 04000 | 02000; break;  /* setuid+setgid */
            case 't': perm |= 01000; break;
            }
            p++;
        }

        /* 按 who 把 owner 位的 perm 复制到 group/other 位 */
        mode_t masked = 0;
        if (who & 1) masked |= perm;
        if (who & 2) masked |= (perm >> 6) & 070;   /* group 的 rwx */
        if (who & 4) masked |= (perm >> 6) & 07;     /* other 的 rwx */
        /* 特殊位：s/t 不随 who 移位 */
        if (who & 1) masked |= perm & 04000;  /* setuid 仅 owner 设置 */
        if (who & 2) masked |= perm & 02000;  /* setgid 仅 group 设置 */
        if (who & 4) masked |= perm & 01000;  /* sticky 仅 other */

        switch (op) {
        case '+': *mode |= masked; break;
        case '-': *mode &= ~masked; break;
        case '=':
            /* 清空 who 对应位再设 */
            if (who & 1) *mode &= ~04700;
            if (who & 2) *mode &= ~02070;
            if (who & 4) *mode &= ~01007;
            *mode |= masked;
            break;
        }

        /* 跳过逗号 */
        if (*p == ',') { p++; continue; }
        if (*p != '\0') {
            fprintf(stderr, "%s: invalid mode: '%s'\n", program_name, spec);
            return -1;
        }
    }
    return 0;
}

static int chmod_one(const char *path, mode_t new_mode, int is_symbolic, const char *spec);

static int chmod_recursive(const char *path, mode_t new_mode, int is_symbolic, const char *spec) {
    struct stat st;
    if (lstat(path, &st) < 0) {
        if (!flag_silent) fprintf(stderr, "%s: %s: %s\n",
                                   program_name, path, strerror(errno));
        return 1;
    }
    /* 当前是符号链接：跳过（不递归） */
    if (S_ISLNK(st.st_mode)) return 0;

    mode_t target = new_mode;
    if (is_symbolic) {
        target = st.st_mode;
        if (apply_symbolic(&target, spec) < 0) return 1;
    }
    if (chmod(path, target) < 0) {
        if (!flag_silent) fprintf(stderr, "%s: %s: %s\n",
                                   program_name, path, strerror(errno));
        return 1;
    }
    if (flag_changes) {
        printf("mode of '%s' changed to %04o\n", path, target & 07777);
    }

    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        if (!d) return 0;
        struct dirent *de;
        while ((de = readdir(d))) {
            if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
            char buf[4096];
            snprintf(buf, sizeof(buf), "%s/%s", path, de->d_name);
            chmod_recursive(buf, new_mode, is_symbolic, spec);
        }
        closedir(d);
    }
    return 0;
}

static int chmod_one(const char *path, mode_t new_mode, int is_symbolic, const char *spec) {
    if (flag_recursive) {
        return chmod_recursive(path, new_mode, is_symbolic, spec);
    }
    struct stat st;
    if (lstat(path, &st) < 0) {
        if (!flag_silent) fprintf(stderr, "%s: %s: %s\n",
                                   program_name, path, strerror(errno));
        return 1;
    }
    mode_t target = new_mode;
    if (is_symbolic) {
        target = st.st_mode;
        if (apply_symbolic(&target, spec) < 0) return 1;
    }
    if (chmod(path, target) < 0) {
        if (!flag_silent) fprintf(stderr, "%s: %s: %s\n",
                                   program_name, path, strerror(errno));
        return 1;
    }
    if (flag_changes) {
        printf("mode of '%s' changed to %04o\n", path, target & 07777);
    }
    return 0;
}

static void usage(void) {
    fprintf(stdout,
        "Usage: %s [OPTION]... MODE[,MODE]... FILE...\n"
        "Change the mode of each FILE to MODE.\n\n"
        "  -R, --recursive  change files and directories recursively\n"
        "  -c, --changes     report each file changed\n"
        "  -f, --silent     suppress most error messages\n"
        "      --classic    POSIX style\n"
        "      --help       display this help and exit\n"
        "      --version    output version information and exit\n\n"
        "MODE is octal (e.g. 755) or symbolic (e.g. u+x,go-w).\n",
        program_name);
    exit(0);
}

int main(int argc, char **argv) {
    set_program_name(argv[0]);
    utils_classic_init(argc, argv);

    static const struct utils_option longopts[] = {
        { "recursive", no_argument, NULL, 'R' },
        { "changes",   no_argument, NULL, 'c' },
        { "silent",    no_argument, NULL, 'f' },
        { "quiet",     no_argument, NULL, 'f' },
        { "classic",   no_argument, NULL, 1000 },
        { "help",      no_argument, NULL, 'h' },
        { "version",   no_argument, NULL, 'V' },
        { NULL, 0, NULL, 0 },
    };

    int opt;
    while ((opt = utils_getopt_long(argc, argv, "RcfhV", longopts, NULL)) != -1) {
        switch (opt) {
        case 'R': flag_recursive = 1; break;
        case 'c': flag_changes = 1; break;
        case 'f': flag_silent = 1; break;
        case 1000: break;
        case 'h': usage(); return 0;
        case 'V': version(); break;
        default:
            fprintf(stderr, "%s: try --help for more information\n", program_name);
            return 2;
        }
    }

    if (argc - utils_optind < 2) {
        fprintf(stderr, "%s: missing operand\n", program_name);
        return 1;
    }

    const char *mode_str = argv[utils_optind];
    mode_t new_mode;
    int is_symbolic;

    mode_t octal = parse_octal(mode_str);
    if (octal != (mode_t)-1) {
        new_mode = octal;
        is_symbolic = 0;
    } else {
        /* 符号模式：先验空 */
        new_mode = 0;
        is_symbolic = 1;
        /* 验证合法性 */
        mode_t test = 0;
        if (apply_symbolic(&test, mode_str) < 0) return 1;
    }

    int rc = 0;
    for (int i = utils_optind + 1; i < argc; i++) {
        int r = chmod_one(argv[i], new_mode, is_symbolic, mode_str);
        if (r) rc = r;
    }
    return rc;
}
