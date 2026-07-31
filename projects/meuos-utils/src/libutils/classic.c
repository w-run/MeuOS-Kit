/* libutils/classic.c - 统一 classic 模式检测
 *
 * 3 路递进兼容策略的入口：
 *   优先级：--classic argv > MSH_CLASSIC env > NO_COLOR env
 * classic=1 时自动关颜色，工具主体只需查 utils_classic_mode 分支。
 */

#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <string.h>

#include "meuos/color.h"
#include "meuos/utils.h"

/* 全局 classic 模式标志：0 = 现代（默认），1 = POSIX 回退 */
int utils_classic_mode = 0;

/* 扫 argv 查 "--classic"（不影响 utils_optind，纯检测） */
static int argv_has_classic(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (argv[i] && strcmp(argv[i], "--classic") == 0) return 1;
    }
    return 0;
}

/* 检测 MSH_CLASSIC 环境变量：值为 "1" 或 "true"（不区分大小写）即生效 */
static int env_classic(void) {
    const char *v = getenv("MSH_CLASSIC");
    if (!v) return 0;
    if (strcmp(v, "1") == 0 || strcmp(v, "true") == 0) return 1;
    if (strcmp(v, "TRUE") == 0) return 1;
    return 0;
}

int utils_classic_init(int argc, char **argv) {
    /* 1. argv --classic 优先 */
    if (argv_has_classic(argc, argv)) {
        utils_classic_mode = 1;
    }
    /* 2. MSH_CLASSIC env */
    else if (env_classic()) {
        utils_classic_mode = 1;
    }

    /* 3. NO_COLOR 只关颜色，不进 classic 模式 */
    if (getenv("NO_COLOR")) {
        color_disable();
    }

    /* classic 模式下强制关颜色 */
    if (utils_classic_mode) {
        color_disable();
    }

    return utils_classic_mode;
}
