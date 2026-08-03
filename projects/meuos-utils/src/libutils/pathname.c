/* libutils/pathname.c — basename/dirname 的安全版本
 *
 * 与 POSIX basename()/dirname() 不同：
 *   - 不修改入参（POSIX 版本是 in-place）
 *   - 不引用 GNU extension basename()（glibc basename() 会 strip suffix）
 *   - 空字符串/纯分隔符都返回 "."
 */

#include <libgen.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "meuos/utils.h"

char *utils_basename(const char *path) {
    if (!path || !*path) return xstrdup(".");

    /* 找到最后一个非分隔符后的字符 */
    size_t len = strlen(path);
    while (len > 0 && path[len - 1] == '/') len--;
    if (len == 0) return xstrdup("/");  /* 全部是 / */

    /* 在剩余段中找最后一个 / */
    const char *last = path;
    for (size_t i = 0; i < len; i++) {
        if (path[i] == '/') last = path + i + 1;
    }
    return xstrndup(last, (size_t)((path + len) - last));
}

char *utils_dirname(const char *path) {
    if (!path || !*path) return xstrdup(".");

    /* 找到最后一个非分隔符 */
    size_t len = strlen(path);
    while (len > 0 && path[len - 1] == '/') len--;
    if (len == 0) return xstrdup("/");

    /* 找最后一个 / */
    const char *p = NULL;
    for (size_t i = 0; i < len; i++) {
        if (path[i] == '/') p = path + i;
    }
    if (!p) return xstrdup(".");
    if (p == path) return xstrdup("/");

    size_t dirlen = (size_t)(p - path);
    while (dirlen > 0 && path[dirlen - 1] == '/') dirlen--;
    return xstrndup(path, dirlen ? dirlen : 1);
}
