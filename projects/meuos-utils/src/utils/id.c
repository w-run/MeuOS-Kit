/* id — 显示用户/组 ID
 * 用法：id [OPTION]... [USER]
 * 选项：-u 仅 UID, -g 仅 GID, -G 所有 GID, -n 显示名称(配合 -u/-g)
 */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char version[] = "0.1.0-id (meuos-utils)";

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "--version")) { printf("id %s\n", version); return 0; }
    if (argc > 1 && !strcmp(argv[1], "--help")) {
        printf("Usage: id [-u] [-g] [-G] [-n] [USER]\n");
        return 0;
    }
    int want_uid = 0, want_gid = 0, want_gids = 0, want_name = 0;
    int argi = 1;
    while (argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0') {
        for (char *p = argv[argi]+1; *p; p++) {
            switch (*p) {
            case 'u': want_uid = 1; break;
            case 'g': want_gid = 1; break;
            case 'G': want_gids = 1; break;
            case 'n': want_name = 1; break;
            default: fprintf(stderr, "id: unknown option -%c\n", *p); return 2;
            }
        }
        argi++;
    }
    uid_t uid = geteuid();
    gid_t gid = getegid();
    const char *username = NULL;
    if (argi < argc) {
        /* 查找指定用户 */
        struct passwd *pw = getpwnam(argv[argi]);
        if (!pw) { fprintf(stderr, "id: %s: no such user\n", argv[argi]); return 1; }
        uid = pw->pw_uid;
        gid = pw->pw_gid;
        username = argv[argi];
    }
    if (want_name && !want_uid && !want_gid && !want_gids) {
        want_uid = 1;
    }
    if (want_uid) {
        if (want_name) {
            struct passwd *pw = getpwuid(uid);
            printf("%s\n", pw ? pw->pw_name : "unknown");
        } else {
            printf("%d\n", uid);
        }
        return 0;
    }
    if (want_gid) {
        if (want_name) {
            struct group *gr = getgrgid(gid);
            printf("%s\n", gr ? gr->gr_name : "unknown");
        } else {
            printf("%d\n", gid);
        }
        return 0;
    }
    if (want_gids) {
        int ngroups = 32;
        gid_t groups[32];
        getgrouplist(username ? username : 
                     (getpwuid(uid) ? getpwuid(uid)->pw_name : ""), gid, groups, &ngroups);
        for (int i = 0; i < ngroups; i++) {
            if (i) putchar(' ');
            if (want_name) {
                struct group *gr = getgrgid(groups[i]);
                printf("%s", gr ? gr->gr_name : "unknown");
            } else {
                printf("%d", groups[i]);
            }
        }
        putchar('\n');
        return 0;
    }
    /* 默认：完整输出 */
    struct passwd *pw = getpwuid(uid);
    struct group *gr = getgrgid(gid);
    printf("uid=%d(%s) gid=%d(%s)", uid, pw ? pw->pw_name : "unknown",
           gid, gr ? gr->gr_name : "unknown");
    /* 辅助组 */
    int ngroups = 32;
    gid_t groups[32];
    if (getgrouplist(pw ? pw->pw_name : "", gid, groups, &ngroups) > 0) {
        printf(" groups=");
        for (int i = 0; i < ngroups; i++) {
            struct group *g = getgrgid(groups[i]);
            if (i) putchar(',');
            printf("%d(%s)", groups[i], g ? g->gr_name : "unknown");
        }
    }
    putchar('\n');
    return 0;
}
