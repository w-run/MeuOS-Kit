/* libutils/getopt.c — GNU 风格长选项解析（getopt_long 子集）
 *
 * 实现接口与 POSIX getopt()/GNU getopt_long() 兼容的子集：
 *   - 短选项：a / a: / a::
 *   - 长选项：utils_option 表（name/has_arg/flag/val）
 *   - globals: utils_optind / utils_optopt / utils_optarg
 *
 * 与 glibc getopt_long 行为差异：
 *   - 仅支持本项目需要的特性：长选项 / 可选参数 / flag=NULL 或非 NULL
 *   - 不支持 permuting argv 顺序（保持 argv 原序）
 *   - 不识别 W; 等 BSD 扩展
 *
 * 设计目的：避免链接 glibc，让所有工具在 mcc + meuos-libc 环境下编译通过。
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "meuos/utils.h"

int utils_optind = 1;
int utils_optopt = 0;
char *utils_optarg = NULL;

/* 内部状态：当前 argv 中正在处理的参数位置（指向非选项的下一个位置） */
static int nextchar = 0;  /* argv[utils_optind] 中的下一个字符索引 */

/* 重置（POSIX getopt 要求） */
void utils_reset_getopt(void) {
    utils_optind = 1;
    utils_optopt = 0;
    utils_optarg = NULL;
    nextchar = 0;
}

/* 在 shortopts 中查找字符 c 是否为合法选项 */
static int find_short(int c, const char *shortopts) {
    for (const char *p = shortopts; *p; p++) {
        if (*p == c) return 1;
    }
    return 0;
}

/* 在 longopts 中查找匹配 name 的条目。
 * exact=1：要求完整匹配；exact=0：允许唯一前缀匹配
 * 返回 longopts 中匹配的索引，或 -1 表示未找到，-2 表示模糊。 */
static int find_long(const char *name, int exact,
                     const struct utils_option *longopts) {
    int match = -1;
    int ambiguous = 0;
    size_t namelen = strlen(name);
    for (int i = 0; longopts[i].name; i++) {
        const char *optname = longopts[i].name;
        if (strcmp(name, optname) == 0) return i;  /* 精确匹配 */
        if (!exact && namelen < strlen(optname)
            && memcmp(name, optname, namelen) == 0) {
            if (match >= 0) {
                ambiguous = 1;
            } else {
                match = i;
            }
        }
    }
    return ambiguous ? -2 : match;
}

int utils_getopt_long(int argc, char * const argv[],
                      const char *shortopts,
                      const struct utils_option *longopts,
                      int *longindex) {
    utils_optarg = NULL;

    /* 跳过非选项参数。当前实现：不重排 argv。
     * 简化行为：处理到下一个 '-' 开头或结束。
     * POSIX getopt 默认重排，glibc 默认也重排，但是本工具集场景
     * 通常 argv 已是工具参数，可接受这种情况。 */

    if (utils_optind >= argc) return -1;

    char *arg = argv[utils_optind];

    /* '--' 终止 */
    if (arg[0] == '-' && arg[1] == '-' && arg[2] == '\0') {
        utils_optind++;
        nextchar = 0;
        return -1;
    }

    /* 长选项：--name 或 --name=value */
    if (arg[0] == '-' && arg[1] == '-' && arg[2] != '\0') {
        const char *name = arg + 2;
        char *eq = strchr(name, '=');
        size_t namelen = eq ? (size_t)(eq - name) : strlen(name);
        char namebuf[64];
        if (namelen >= sizeof(namebuf)) {
            /* 长选项名过长，按未知处理 */
            fprintf(stderr, "%s: unrecognized option '%s'\n",
                    program_name ? program_name : "utils", arg);
            utils_optopt = 0;
            utils_optind++;
            return '?';
        }
        memcpy(namebuf, name, namelen);
        namebuf[namelen] = '\0';

        int idx = find_long(namebuf, 0, longopts);
        if (idx == -1 || idx == -2) {
            fprintf(stderr, "%s: unrecognized option '--%s'\n",
                    program_name ? program_name : "utils", namebuf);
            utils_optopt = 0;
            utils_optind++;
            return '?';
        }

        const struct utils_option *o = &longopts[idx];
        if (longindex) *longindex = idx;

        /* 处理参数：有 = 直接取；否则看 has_arg */
        char *val = NULL;
        int consumed_argv = 0;

        if (eq) {
            val = eq + 1;
        } else if (o->has_arg == 1) {
            /* 需要参数：取下一个 argv */
            if (utils_optind + 1 >= argc) {
                fprintf(stderr, "%s: option '--%s' requires an argument\n",
                        program_name ? program_name : "utils", namebuf);
                utils_optopt = 0;
                utils_optind++;
                return '?';
            }
            val = argv[utils_optind + 1];
            consumed_argv = 1;
        }

        utils_optarg = val;
        utils_optind += 1 + consumed_argv;
        nextchar = 0;

        if (o->flag) {
            *o->flag = o->val;
            return 0;
        }
        return o->val;
    }

    /* 短选项：以 '-' 开头但不是 '-' 或 '--' */
    if (arg[0] == '-' && arg[1] != '\0') {
        if (nextchar == 0) nextchar = 1;

        int c = (unsigned char)arg[nextchar];
        if (!find_short(c, shortopts)) {
            fprintf(stderr, "%s: invalid option -- '%c'\n",
                    program_name ? program_name : "utils", c);
            utils_optopt = c;
            /* 推进：吃掉整个 token 或只到坏选项，GNU 行为为后者 */
            if (arg[nextchar + 1] == '\0') {
                utils_optind++;
                nextchar = 0;
            } else {
                nextchar++;
            }
            return '?';
        }

        /* 找短选项的 has_arg（在 shortopts 中查 'a:' / 'a::'） */
        const char *p = strchr(shortopts, c);
        int has_arg = 0;
        int optional = 0;
        if (p[1] == ':') {
            has_arg = 1;
            if (p[2] == ':') optional = 1;
        }

        if (has_arg) {
            char *val = NULL;
            if (arg[nextchar + 1]) {
                /* 同行内剩余部分作为参数 */
                val = &arg[nextchar + 1];
                utils_optind++;
                nextchar = 0;
            } else if (optional) {
                /* 可选参数：未提供 */
                val = NULL;
                utils_optind++;
                nextchar = 0;
            } else {
                /* 必填：取下一个 argv */
                if (utils_optind + 1 >= argc) {
                    fprintf(stderr, "%s: option requires an argument -- '%c'\n",
                            program_name ? program_name : "utils", c);
                    utils_optopt = c;
                    utils_optind++;
                    nextchar = 0;
                    return '?';
                }
                val = argv[utils_optind + 1];
                utils_optind += 2;
                nextchar = 0;
            }
            utils_optarg = val;
        } else {
            /* 无参数：可能联动下一个字符 */
            if (arg[nextchar + 1]) {
                nextchar++;
            } else {
                utils_optind++;
                nextchar = 0;
            }
        }
        return c;
    }

    /* 非选项：跳过整段非选项 argv，回到调用方处理。
     * 简化行为：返回 -1，剩余 argv 由工具 main 处理。 */
    return -1;
}
