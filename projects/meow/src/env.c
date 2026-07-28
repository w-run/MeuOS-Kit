/* meow - env: print build-environment overview (neofetch-style). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/wait.h>

#include "meow.h"

/* Run a command, capture up to cap bytes of its stdout, and write into buf.
 * Returns 0 on success (command exit==0), -1 on error. */
static int
capture(const char *cmd, char *buf, size_t cap)
{
    FILE *fp = popen(cmd, "r");
    if (!fp)
        return -1;
    size_t n = fread(buf, 1, cap - 1, fp);
    buf[n] = '\0';
    /* Strip trailing newline. */
    while (n > 0 && buf[n - 1] == '\n')
        buf[--n] = '\0';
    int rc = pclose(fp);
    return (rc == 0 && WIFEXITED(rc)) ? 0 : -1;
}

/* Count .h files under a directory (recursive, scan up to a limit). */
static int
count_headers(const char *dir)
{
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "find \"%s\" -name '*.h' 2>/dev/null | wc -l", dir);
    char buf[64];
    if (capture(cmd, buf, sizeof(buf)) != 0)
        return -1;
    return atoi(buf);
}

int
cmd_env(void)
{
    char buf[1024];
    struct utsname uts;
    int all_ok = 1;

    printf("  meow v0.4.0\n");

    /* 1. CC detection. */
    const char *cc = getenv("CC");
    if (!cc) cc = "mcc";
    snprintf(buf, sizeof(buf), "%s --version 2>/dev/null | head -1", cc);
    char ver[256];
    int cc_ok = (capture(buf, ver, sizeof(ver)) == 0);
    if (cc_ok && strlen(ver) > 0) {
        printf("  %s: %s\n", color_cyan("CC"), ver);
    } else if (g_color_enabled) {
        printf("  %s: %s (%s)\n", color_cyan("CC"), color_red(cc), color_red("not found"));
        all_ok = 0;
    } else {
        printf("  CC: %s (not found)\n", cc);
        all_ok = 0;
    }

    /* 2. Architecture detection. */
    char arch[64] = "x86_64";
    if (uname(&uts) == 0) {
        const char *m = uts.machine;
        snprintf(arch, sizeof(arch), "%s", m);
    }
    const char *target = getenv("MEUOS_ARCH");
    printf("  %s: %s %s\n", color_cyan("架构"), arch,
           target ? color_gray("(宿主)") : color_gray("(宿主)"));

    /* 3. Sysroot detection. */
    const char *sr = getenv("MEUOS_SYSROOT");
    int sr_ok = 0;
    if (sr) {
        char test_path[2048];
        snprintf(test_path, sizeof(test_path), "%s/usr/lib/crt1.o", sr);
        sr_ok = (access(test_path, F_OK) == 0);
    }
    if (sr_ok) {
        printf("  %s: %s (%s)\n", color_cyan("sysroot"), sr,
               color_green("完整"));
    } else if (sr) {
        printf("  %s: %s (%s)\n", color_cyan("sysroot"), sr,
               g_color_enabled ? color_red("不完整") : "不完整");
        all_ok = 0;
    } else {
        printf("  %s: %s\n", color_cyan("sysroot"),
               g_color_enabled ? color_red("未设置 MEUOS_SYSROOT") : "未设置 MEUOS_SYSROOT");
        all_ok = 0;
    }

    /* 4. Headers. */
    if (sr_ok) {
        char inc_dir[2048];
        snprintf(inc_dir, sizeof(inc_dir), "%s/usr/include", sr);
        int nh = count_headers(inc_dir);
        if (nh > 0)
            printf("  %s: %s (%d 个)\n", color_cyan("头文件"), inc_dir, nh);
        else
            printf("  %s: %s (%s)\n", color_cyan("头文件"), inc_dir,
                   g_color_enabled ? color_yellow("空") : "空");
    }

    /* 5. Library detection. */
    if (sr_ok) {
        char lib_dir[2048], libs[1024] = "";
        snprintf(lib_dir, sizeof(lib_dir), "%s/usr/lib", sr);
        int has_core = (access(lib_dir, F_OK) == 0);
        int has_core_a = 0, has_compat_a = 0;
        if (has_core) {
            char p[2048];
            snprintf(p, sizeof(p), "%s/libc-meuos.a", lib_dir);
            has_core_a = (access(p, F_OK) == 0);
            snprintf(p, sizeof(p), "%s/libc-meuos-compat.a", lib_dir);
            has_compat_a = (access(p, F_OK) == 0);
        }
        if (has_core_a && has_compat_a)
            printf("  %s: %s\n", color_cyan("库"),
                   "libc-meuos.a + libc-meuos-compat.a");
        else if (has_core_a)
            printf("  %s: %s\n", color_cyan("库"), "libc-meuos.a");
        else
            printf("  %s: %s\n", color_cyan("库"),
                   g_color_enabled ? color_yellow("未找到") : "未找到");
    }

    /* 6. Toolchain detection. */
    const char *mt_as = getenv("MT_AS");
    const char *mt_ld = getenv("MT_LD");
    const char *tc_str;
    int mt_ok = 0;
    if (mt_as && mt_ld) {
        tc_str = "mt/as + mt/ld";
        mt_ok = 1;
    } else if (mt_as) {
        tc_str = "mt/as (未设 MT_LD)";
    } else if (mt_ld) {
        tc_str = "mt/ld (未设 MT_AS)";
    } else {
        tc_str = g_color_enabled ? color_yellow("host cc (未设 MT_AS/MT_LD)") : "host cc (未设 MT_AS/MT_LD)";
    }
    printf("  %s: %s\n", color_cyan("工具链"), tc_str);

    return all_ok ? 0 : 1;
}
