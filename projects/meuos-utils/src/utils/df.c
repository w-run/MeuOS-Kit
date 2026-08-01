/* df — 磁盘空闲空间报告
 * 用法：df [OPTION]... [FILE]...
 * 选项：-h 人类可读, -k KB(默认), -m MB, -i inode 信息
 */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <mntent.h>

static const char version[] = "0.1.0-df (meuos-utils)";

static void print_size(unsigned long long sz, int human, const char *unit) {
    if (human) {
        if (sz >= 1073741824ULL) printf("%4.1fG", (double)sz / 1073741824.0);
        else if (sz >= 1048576) printf("%4.1fM", (double)sz / 1048576.0);
        else if (sz >= 1024) printf("%4.1fK", (double)sz / 1024.0);
        else printf("%4llu", sz);
    } else {
        printf("%llu", sz);
    }
}

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "--version")) { printf("df %s\n", version); return 0; }
    if (argc > 1 && !strcmp(argv[1], "--help")) { printf("Usage: df [-h] [-k|-m] [-i] [FILE]...\n"); return 0; }
    int human = 0, unit_k = 1, unit_m = 0, inodes = 0;
    int argi = 1;
    while (argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0') {
        for (char *p = argv[argi]+1; *p; p++) {
            switch (*p) {
            case 'h': human = 1; break;
            case 'k': unit_k = 1; unit_m = 0; break;
            case 'm': unit_m = 1; unit_k = 0; break;
            case 'i': inodes = 1; break;
            default: fprintf(stderr, "df: unknown option -%c\n", *p); return 2;
            }
        }
        argi++;
    }

    printf("Filesystem     %-8s %-8s %-8s %-5s%% %-s\n",
           inodes ? "Inodes" : "1K-blocks", inodes ? "Iused" : "Used",
           inodes ? "Iavail" : "Avail", "Use", "Mounted on");

    if (argi < argc) {
        /* 报告指定文件所在挂载点 */
        for (int i = argi; i < argc; i++) {
            struct statvfs vfs;
            if (statvfs(argv[i], &vfs) < 0) {
                fprintf(stderr, "df: %s: %s\n", argv[i], strerror(errno));
                continue;
            }
            unsigned long long total = vfs.f_blocks * vfs.f_frsize;
            unsigned long long avail = vfs.f_bavail * vfs.f_frsize;
            unsigned long long used = total - avail;
            int pct = total > 0 ? (int)((used * 100) / total) : 0;
            printf("%-15s ", argv[i]);
            print_size(total / 1024, human, "K");
            printf(" ");
            print_size(used / 1024, human, "K");
            printf(" ");
            print_size(avail / 1024, human, "K");
            printf(" %3d%% %s\n", pct, argv[i]);
        }
    } else {
        /* 遍历 /etc/mtab / /proc/mounts */
        FILE *mtab = setmntent("/proc/mounts", "r");
        if (!mtab) mtab = setmntent("/etc/mtab", "r");
        if (!mtab) { fprintf(stderr, "df: cannot read mount table\n"); return 1; }
        struct mntent *e;
        while ((e = getmntent(mtab))) {
            struct statvfs vfs;
            if (statvfs(e->mnt_dir, &vfs) < 0) continue;
            unsigned long long total = vfs.f_blocks * vfs.f_frsize;
            unsigned long long avail = vfs.f_bavail * vfs.f_frsize;
            unsigned long long used = total - avail;
            int pct = total > 0 ? (int)((used * 100) / total) : 0;
            printf("%-15s ", e->mnt_fsname);
            print_size(total / 1024, human, "K");
            printf(" ");
            print_size(used / 1024, human, "K");
            printf(" ");
            print_size(avail / 1024, human, "K");
            printf(" %3d%% %s\n", pct, e->mnt_dir);
        }
        endmntent(mtab);
    }
    return 0;
}
