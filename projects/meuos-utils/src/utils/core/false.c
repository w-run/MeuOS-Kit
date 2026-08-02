/* false — 永远返回非 0（惯例为 1）
 *
 * POSIX 指定 `false` 是无需选项和操作数的命令。
 */

#include <stdio.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return 1;
}
