/* libutils.h — meuos-utils 公共 API
 *
 * 暴露 libutils.a 的公共接口，供各工具源文件 include 使用。
 * 设计原则：
 *   - x* (xmalloc/xstrdup/xrealloc) 在 OOM 时调用 die() 退出
 *   - getopt_long GNU 长选项解析
 *   - version/program_name 自动初始化（--version/--help 用）
 *   - 零 GNU 专有符号（§4 强约束）
 */
#ifndef MEUOS_UTILS_H
#define MEUOS_UTILS_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* program_name 由 set_program_name() 在 main 开头调用设置。
 * 默认值是 argv[0]，但 set_program_name 会去掉目录前缀，
 * 只保留 basename。供 usage()/version() 用。 */
extern const char *program_name;

/* 版本号，--version 输出来源。仅声明，定义见 libutils/version.c */
extern const char *meuos_utils_version;

/* OOM 错误处理：format + 变参，输出到 stderr + exit(2)。
 * 不返回。形似 warn()/err() 但统一为 die()。 */
void die(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));

/* 设置 program_name。应在每个工具 main() 开头调用一次。
 * 自动从 argv[0] 提取 basename（去掉路径前缀）。 */
void set_program_name(const char *argv0);

/* 输出工具的 --version 行：
 *   <progname> (meuos-utils) <version>\n
 * 后接 LICENSE 信息，然后 exit(0)。 */
void version(void) __attribute__((noreturn));

/* 输出版本信息到 fp，不退出。 */
void print_version(FILE *fp);

/* 长选项解析（兼容 GNU getopt_long 行为）。
 * 简化的子集，仅满足当前工具需要。详细文档见 libutils/getopt.c。
 *
 * shortopts 字符串每个字符：
 *   'a'    短选项 -a（无参数）
 *   'a:'   短选项 -a <arg>
 *   'a::'  短选项 -a 可选 <arg>（GNU 扩展）
 *
 * longopts 与 shortopts 必须同时给出，longindex 同。
 * 返回值与 getopt_long 一致：'?' = 错误 / -1 = 结束 / 0 = long-only / 其他 = 短选项字符 */
extern int utils_optind;     /* 同 getopt: 下一个 argv index，初值 1 */
extern int utils_optopt;     /* 未知选项字符 */
extern char *utils_optarg;   /* 当前选项的参数值 */

struct utils_option {
    const char *name;        /* 长选项名，如 "version" */
    int has_arg;              /* 0 = 无 / 1 = 必填 / 2 = 可选 */
    int *flag;                /* NULL 时 val 作为返回值；非 NULL 时 *flag=val */
    int val;                  /* 见上文 flag 说明 */
};

int utils_getopt_long(int argc, char * const argv[],
                      const char *shortopts,
                      const struct utils_option *longopts,
                      int *longindex);

/* x* 分配器：OOM 时直接 die()。 */
void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t s);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);

/* 简化的 basename()/dirname()：不修改入参，malloc 返回新串。 */
char *utils_basename(const char *path);
char *utils_dirname(const char *path);

/* 人类可读字节数：1024 → "1.0K"。用于 ls -h、df -h。
 * 返回 malloc 的字符串，调用者负责 free()。 */
char *human_readable(uint64_t bytes, int si);

#ifdef __cplusplus
}
#endif

#endif /* MEUOS_UTILS_H */
