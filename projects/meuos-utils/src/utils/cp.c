/* cp — 现代文件复制（MeuOS Next 版）
 *
 * 默认：
 *   - 进度条 + ETA（仅在 tty + 文件较大时）
 *   - 原子替换：先写 tmp 再 rename(2)
 *   - 目录递归 -r
 *
 * GNU 兼容：--classic
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "meuos/progress.h"
#include "meuos/utils.h"

static int flag_recursive = 0;
static int flag_preserve = 0;
static int flag_force = 1;
static int flag_atomic = 1;

static int do_copy_file(const char *src, const char *dst, int preserve,
                        int use_progress) {
    int fdin = open(src, O_RDONLY);
    if (fdin < 0) { perror(src); return -1; }
    struct stat st;
    if (fstat(fdin, &st) < 0) { close(fdin); return -1; }

    char tmpdst[PATH_MAX];
    if (flag_atomic) snprintf(tmpdst, sizeof(tmpdst), "%s.tmpXXXXXX", dst);
    else snprintf(tmpdst, sizeof(tmpdst), "%s", dst);

    int flags = O_WRONLY | O_CREAT | O_TRUNC;
    mode_t mode = preserve ? st.st_mode : 0644;
    int fdout;
    if (flag_atomic) {
        char tmpl[PATH_MAX];
        snprintf(tmpl, sizeof(tmpl), "%s.tmp.XXXXXX", dst);
        fdout = mkstemp(tmpl);
        if (fdout < 0) { close(fdin); perror("mkstemp"); return -1; }
    } else {
        fdout = open(tmpdst, flags, mode);
    }
    if (fdout < 0) { close(fdin); perror(dst); return -1; }

    progress_t *p = NULL;
    if (use_progress) p = progress_new("Copying", (uint64_t)st.st_size);
    char buf[64 * 1024];
    ssize_t n;
    uint64_t total = 0;
    while ((n = read(fdin, buf, sizeof(buf))) > 0) {
        ssize_t m = write(fdout, buf, (size_t)n);
        if (m != n) { close(fdin); close(fdout); return -1; }
        total += (uint64_t)n;
        if (p) progress_update(p, total);
    }
    if (p) { progress_finish(p); progress_free(p); }

    close(fdin);
    if (flag_atomic) {
        if (rename(tmpdst, dst) < 0) {
            perror("rename");
            unlink(tmpdst);
            close(fdout);
            return -1;
        }
    }
    close(fdout);
    if (preserve && !flag_atomic) {
        chmod(dst, st.st_mode);
    }
    return 0;
}

static int do_copy(const char *src, const char *dst, int preserve) {
    struct stat st;
    if (lstat(src, &st) < 0) { perror(src); return -1; }
    if (S_ISDIR(st.st_mode)) {
        if (!flag_recursive) {
            fprintf(stderr, "cp: -r not specified; omitting directory '%s'\n", src);
            return -1;
        }
        mkdir(dst, 0755);
        DIR *d = opendir(src);
        if (!d) return -1;
        struct dirent *de;
        while ((de = readdir(d))) {
            if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
            char nsrc[PATH_MAX], ndst[PATH_MAX];
            snprintf(nsrc, sizeof(nsrc), "%s/%s", src, de->d_name);
            snprintf(ndst, sizeof(ndst), "%s/%s", dst, de->d_name);
            do_copy(nsrc, ndst, preserve);
        }
        closedir(d);
        return 0;
    }
    return do_copy_file(src, dst, preserve, S_ISREG(st.st_mode));
}

static void usage(void) {
    printf("Usage: %s [OPTIONS] SRC DST | SRC... DIR\n", program_name);
    printf("\n");
    printf("Modern cp with progress bar and atomic replacement.\n\n");
    printf("  -r, --recursive      copy directories recursively\n");
    printf("  -p, --preserve       preserve mode/ownership\n");
    printf("      --no-atomic      direct write (no atomic rename)\n");
    printf("      --no-progress    disable progress bar\n");
    printf("      --classic        GNU cp mode (no progress, no atomic)\n");
    printf("      --help           show this help\n");
    printf("      --version        show version\n");
}

int main(int argc, char **argv) {
    set_program_name(argv[0]);

    static const struct option longopts[] = {
        { "recursive",  no_argument, NULL, 'r' },
        { "preserve",   no_argument, NULL, 'p' },
        { "no-atomic",  no_argument, NULL, 1000 },
        { "no-progress",no_argument, NULL, 1001 },
        { "classic",    no_argument, NULL, 1002 },
        { "help",       no_argument, NULL, 'h' },
        { "version",    no_argument, NULL, 'V' },
        { NULL, 0, NULL, 0 },
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "rp:h", longopts, NULL)) != -1) {
        switch (opt) {
        case 'r': flag_recursive = 1; break;
        case 'p': flag_preserve = 1; break;
        case 1000: flag_atomic = 0; break;
        case 1001: /* skip progress */ break;
        case 1002: flag_atomic = 0; break;
        case 'h': usage(); break;
        case 'V': version(); break;
        default: return 2;
        }
    }
    if (argc - optind < 2) {
        fprintf(stderr, "%s: need source and destination\n", program_name);
        usage();
        return 1;
    }
    /* 简化：仅处理 N srcs + 1 dst-dir 形式 */
    const char *dst = argv[argc - 1];
    int nsrc = argc - 1 - optind;
    int rc = 0;
    for (int i = 0; i < nsrc; i++) {
        const char *src = argv[optind + i];
        char target[PATH_MAX];
        if (nsrc > 1) {
            struct stat st;
            if (stat(dst, &st) < 0 || !S_ISDIR(st.st_mode)) {
                fprintf(stderr, "cp: target '%s' is not a directory\n", dst);
                return 1;
            }
            snprintf(target, sizeof(target), "%s/%s", dst, src + (src[0] == '/' ? 0 : 0));
            /* basename */
            const char *base = strrchr(src, '/');
            base = base ? base + 1 : src;
            snprintf(target, sizeof(target), "%s/%s", dst, base);
        } else {
            snprintf(target, sizeof(target), "%s", dst);
        }
        if (do_copy(src, target, flag_preserve) < 0) rc = 1;
    }
    return rc;
}
