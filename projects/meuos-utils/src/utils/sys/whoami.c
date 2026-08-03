/* whoami — 显示当前用户名
 * 用法：whoami
 */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "meuos/utils.h"

int main(int argc, char **argv) {
    int argi = utils_init(argc, argv);
    if (argi < argc && !strcmp(argv[argi], "--help")) utils_usage("Usage: whoami\n");
    (void)argc; (void)argv;
    uid_t uid = geteuid();
    struct passwd *pw = getpwuid(uid);
    if (!pw) { fprintf(stderr, "whoami: cannot find name for UID %d\n", uid); return 1; }
    puts(pw->pw_name);
    return 0;
}
