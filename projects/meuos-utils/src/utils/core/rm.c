/* rm — 删除（MeuOS Next 版）
 *
 * 默认：先尝试移动到 trash（~/.local/share/Trash/files/）；
 * 失败或 -f 时硬删除。
 *
 * --classic: GNU rm 直接硬删（无 trash）
 * -r: 递归
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

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
static int flag_force = 0;
static int flag_trash = 1;  /* 默认 trash */
static int flag_classic = 0;

static const char *trash_dir(void) {
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    static char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/.local/share/Trash/files", home);
    mkdir(path, 0755);
    return path;
}

static int trash_file(const char *src) {
    const char *td = trash_dir();
    char target[PATH_MAX];
    snprintf(target, sizeof(target), "%s/%s", td, src + (src[0] == '/' ? 0 : 0));
    const char *base = strrchr(src, '/');
    base = base ? base + 1 : src;
    char candidate[PATH_MAX];
    snprintf(candidate, sizeof(candidate), "%s/%s", td, base);
    /* 唯一名 */
    int suffix = 0;
    while (access(candidate, F_OK) == 0) {
        snprintf(candidate, sizeof(candidate), "%s/%s.%d", td, base, ++suffix);
    }
    if (rename(src, candidate) == 0) {
        fprintf(stderr, "rm: trashed '%s' → '%s'\n", src, candidate);
        return 0;
    }
    return -1;
}

static int do_rm(const char *path) {
    struct stat st;
    if (lstat(path, &st) < 0) {
        if (!flag_force) perror(path);
        return -1;
    }
    if (S_ISDIR(st.st_mode)) {
        if (!flag_recursive) {
            fprintf(stderr, "rm: cannot remove '%s': Is a directory\n", path);
            return -1;
        }
        DIR *d = opendir(path);
        if (!d) { perror(path); return -1; }
        struct dirent *de;
        while ((de = readdir(d))) {
            if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
            char npath[PATH_MAX];
            snprintf(npath, sizeof(npath), "%s/%s", path, de->d_name);
            do_rm(npath);
        }
        closedir(d);
        if (rmdir(path) < 0 && !flag_force) perror(path);
        return 0;
    }
    if (flag_trash && !flag_classic) {
        if (trash_file(path) == 0) return 0;
    }
    if (unlink(path) < 0) {
        if (!flag_force) perror(path);
        return -1;
    }
    return 0;
}

static void usage(void) {
    printf("Usage: %s [OPTIONS] FILE...\n", program_name);
    printf("\n");
    printf("Modern rm: defaults to trash; pass -f for hard delete.\n");
    printf("\n");
    printf("  -r, --recursive     remove directories recursively\n");
    printf("  -f, --force         force; bypass trash, ignore missing\n");
    printf("      --no-trash      hard delete (no trash)\n");
    printf("      --classic       GNU rm mode (no trash, no confirmation)\n");
    printf("      --help          show this help\n");
    printf("      --version       show version\n");
    printf("\n");
    printf("Trash directory: ~/.local/share/Trash/files/\n");
}

int main(int argc, char **argv) {
    set_program_name(argv[0]);

    static const struct option longopts[] = {
        { "recursive", no_argument, NULL, 'r' },
        { "force",     no_argument, NULL, 'f' },
        { "no-trash",  no_argument, NULL, 1000 },
        { "classic",   no_argument, NULL, 1001 },
        { "help",      no_argument, NULL, 'h' },
        { "version",   no_argument, NULL, 'V' },
        { NULL, 0, NULL, 0 },
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "rfh", longopts, NULL)) != -1) {
        switch (opt) {
        case 'r': flag_recursive = 1; break;
        case 'f': flag_force = 1; flag_trash = 0; break;
        case 1000: flag_trash = 0; break;
        case 1001: flag_trash = 0; flag_classic = 1; break;
        case 'h': usage(); break;
        case 'V': version(); break;
        default: return 2;
        }
    }
    if (optind >= argc) {
        fprintf(stderr, "%s: missing operand\n", program_name);
        usage();
        return 1;
    }
    int rc = 0;
    for (int i = optind; i < argc; i++) {
        if (do_rm(argv[i]) < 0) rc = 1;
    }
    return rc;
}
