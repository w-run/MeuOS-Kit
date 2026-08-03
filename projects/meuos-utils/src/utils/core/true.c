/* true — 永远返回 0
 *
 * 最简单的工具之一，用作 libutils.a 烟雾测试入口。
 * POSIX 指定 `true` 是无需选项和操作数的命令。
 */

#include <stdio.h>

#include "meuos/utils.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return 0;
}
