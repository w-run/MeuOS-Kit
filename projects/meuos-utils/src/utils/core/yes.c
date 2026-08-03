/* yes — 无限输出指定字符串（默认 "y"）
 *
 * POSIX: yes [string...]
 * 用法：yes | head -5  →  输出一行后被 head 截断
 *
 * 实现简单的循环输出，不读 stdin（与 GNU yes 一致）。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "meuos/utils.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    const char *msg = (argc > 1) ? argv[1] : "y";
    size_t len = strlen(msg);

    /* 缓冲一行，减少 write() 次数 */
    char *line = xmalloc(len + 2);
    memcpy(line, msg, len);
    line[len] = '\n';
    line[len + 1] = '\0';

    while (1) {
        fputs(line, stdout);
        if (ferror(stdout)) break;  /* pipe 关闭等情况 */
    }

    free(line);
    return 0;
}
