/* chown — 修改文件属主/属组
 *
 * 支持：
 *   chown USER FILE...
 *   chown USER:GROUP FILE...
 *   chown :GROUP FILE...
 *   chown USER: FILE...  (group = user's group)
 *   -R 递归
 *   -v verbose
 *   --reference=RFILE  从参考文件获取 owner:group
 *   --help / --version
 *
 * USER 和 GROUP 可以是数字 UID/GID 或用户名/组名（通过 getpwnam/getgrnam）。
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "meuos/utils.h"

static int flag_recursive = 0;
static int flag_verbose = 0;

static void usage(void) {
    printf("Usage: %s [OPTIONS] USER[:GROUP] FILE...\n", program_name);
    printf("       %s [OPTIONS] :GROUP FILE...\n", program_name);
    printf("       %s [OPTIONS] --reference=RFILE FILE...\n\n", program_name);
    printf("Change file owner and/or group.\n\n");
    printf("  -R, --recursive    operate on files and directories recursively\n");
    printf("  -v, --verbose      print what was done\n");
    printf("      --reference=FILE  use FILE's owner/group\n");
    printf("      --help         show this help\n");
    printf("      --version      show version\n");
}

/* Parse owner:group specification */
static int parse_spec(const char *spec, uid_t *uid, gid_t *gid, int *set_uid, int *set_gid) {
    *set_uid = 0;
    *set_gid = 0;
    
    char *copy = strdup(spec);
    char *colon = strchr(copy, ':');
    
    char *user_str = copy;
    char *group_str = NULL;
    
    if (colon) {
        *colon = '\0';
        group_str = colon + 1;
        /* Handle "user:" (empty group = user's primary group) */
        /* Handle ":group" (empty user = only change group) */
    }
    
    /* Parse user */
    if (user_str && *user_str) {
        /* Try numeric UID first */
        int all_digit = 1;
        for (char *p = user_str; *p; p++) {
            if (!isdigit((unsigned char)*p)) { all_digit = 0; break; }
        }
        
        if (all_digit) {
            *uid = (uid_t)atoi(user_str);
            *set_uid = 1;
        } else {
            struct passwd *pw = getpwnam(user_str);
            if (!pw) {
                fprintf(stderr, "%s: invalid user: '%s'\n", program_name, user_str);
                free(copy);
                return -1;
            }
            *uid = pw->pw_uid;
            *set_uid = 1;
            /* If group not specified and user has colon (user:), use user's primary group */
            if (colon && !group_str[0]) {
                *gid = pw->pw_gid;
                *set_gid = 1;
            }
        }
    }
    
    /* Parse group */
    if (group_str && *group_str) {
        int all_digit = 1;
        for (char *p = group_str; *p; p++) {
            if (!isdigit((unsigned char)*p)) { all_digit = 0; break; }
        }
        
        if (all_digit) {
            *gid = (gid_t)atoi(group_str);
            *set_gid = 1;
        } else {
            struct group *gr = getgrnam(group_str);
            if (!gr) {
                fprintf(stderr, "%s: invalid group: '%s'\n", program_name, group_str);
                free(copy);
                return -1;
            }
            *gid = gr->gr_gid;
            *set_gid = 1;
        }
    }
    
    free(copy);
    return 0;
}

static int do_chown(const char *path, uid_t uid, gid_t gid, int set_uid, int set_gid) {
    struct stat st;
    if (stat(path, &st) != 0) {
        fprintf(stderr, "%s: cannot access '%s': %s\n", program_name, path, strerror(errno));
        return 1;
    }
    
    /* If only changing one of uid/gid, keep the other */
    uid_t new_uid = set_uid ? uid : st.st_uid;
    gid_t new_gid = set_gid ? gid : st.st_gid;
    
    if (lchown(path, new_uid, new_gid) != 0) {
        fprintf(stderr, "%s: changing ownership of '%s': %s\n", program_name, path, strerror(errno));
        return 1;
    }
    
    if (flag_verbose) {
        char owner[64], group[64];
        struct passwd *pw = getpwuid(new_uid);
        struct group *gr = getgrgid(new_gid);
        if (pw) snprintf(owner, sizeof(owner), "%s", pw->pw_name);
        else snprintf(owner, sizeof(owner), "%d", new_uid);
        if (gr) snprintf(group, sizeof(group), "%s", gr->gr_name);
        else snprintf(group, sizeof(group), "%d", new_gid);
        printf("changed ownership of '%s' to %s:%s\n", path, owner, group);
    }
    
    /* Recurse into directories */
    if (flag_recursive && S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        if (d) {
            struct dirent *ent;
            while ((ent = readdir(d)) != NULL) {
                if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
                    continue;
                char child[4096];
                snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
                do_chown(child, uid, gid, set_uid, set_gid);
            }
            closedir(d);
        }
    }
    
    return 0;
}

int main(int argc, char **argv) {
    set_program_name(argv[0]);
    
    const char *reference = NULL;
    
    static const struct option longopts[] = {
        { "recursive",  no_argument,       NULL, 'R' },
        { "verbose",    no_argument,       NULL, 'v' },
        { "reference",  required_argument, NULL, 'r' },
        { "help",       no_argument,       NULL, 'h' },
        { "version",    no_argument,       NULL, 'V' },
        { NULL, 0, NULL, 0 },
    };
    
    int opt;
    while ((opt = getopt_long(argc, argv, "Rvh", longopts, NULL)) != -1) {
        switch (opt) {
        case 'R': flag_recursive = 1; break;
        case 'v': flag_verbose = 1; break;
        case 'r': reference = optarg; break;
        case 'h': usage(); return 0;
        case 'V': version(); return 0;
        default: return 2;
        }
    }
    
    if (optind >= argc) {
        fprintf(stderr, "%s: missing operand\n", program_name);
        usage();
        return 2;
    }
    
    uid_t uid = 0;
    gid_t gid = 0;
    int set_uid = 0, set_gid = 0;
    
    if (reference) {
        struct stat st;
        if (stat(reference, &st) != 0) {
            fprintf(stderr, "%s: cannot stat '%s': %s\n", program_name, reference, strerror(errno));
            return 1;
        }
        uid = st.st_uid;
        gid = st.st_gid;
        set_uid = 1;
        set_gid = 1;
    } else {
        /* owner:group spec is the first non-option argument */
        if (parse_spec(argv[optind], &uid, &gid, &set_uid, &set_gid) != 0)
            return 1;
        optind++;
    }
    
    if (optind >= argc) {
        fprintf(stderr, "%s: missing file operand\n", program_name);
        return 2;
    }
    
    int ret = 0;
    for (int i = optind; i < argc; i++) {
        if (do_chown(argv[i], uid, gid, set_uid, set_gid) != 0)
            ret = 1;
    }
    
    return ret;
}
