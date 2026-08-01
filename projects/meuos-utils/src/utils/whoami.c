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

static const char version[] = "0.1.0-whoami (meuos-utils)";

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "--version")) { printf("whoami %s\n", version); return 0; }
    if (argc > 1 && !strcmp(argv[1], "--help")) { printf("Usage: whoami\n"); return 0; }
    (void)argv;
    uid_t uid = geteuid();
    struct passwd *pw = getpwuid(uid);
    if (!pw) { fprintf(stderr, "whoami: cannot find name for UID %d\n", uid); return 1; }
    puts(pw->pw_name);
    return 0;
}
