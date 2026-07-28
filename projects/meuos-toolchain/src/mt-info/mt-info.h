/* mt-info - 统一 ELF 分析工具（MeuOS 现代化工具箱）
 *
 * 取代 9 个碎片工具：size/strings/addr2line/ldd/nm/readelf/objdump/strip/objcopy。
 * 不是"又一个 objdump"，而是 ELF 文件的一站式分析入口，通过子命令区分能力。
 *
 * 设计原则（见 AGENTS.md §10.4 / p9-ui）：
 *   - 不做传家宝：不克隆 GNU 工具各自的命令行语法
 *   - 现代化体验：彩色信息卡 + TUI 交互 + --json 一等公民
 *   - 跨工具一致：--json/--quiet/--verbose/--no-color 统一语义（见 p9-ui 约定）
 *
 * 子命令：
 *   mt info <elf>      信息卡：类型/架构/入口/节区/符号/依赖一览
 *   mt headers <elf>   快速 ELF/节区头查看
 *   mt deps <elf>      递归 DT_NEEDED 依赖树
 *   mt strings <elf>   智能字符串提取（去重/过滤 ASCII 噪音）
 *   mt which <sym>     搜索符号：哪个库里定义了 sym？
 *   mt diff <a> <b>    语义 diff：节区大小/符号增减/重定位差异
 *   mt inspect <elf>   TUI 交互式浏览（节区/符号/重定位/依赖/字符串）
 */
#ifndef MT_INFO_H
#define MT_INFO_H

#include "mt/elf.h"
#include <stdio.h>
#include <stddef.h>

#define MT_INFO_VERSION "0.1.0"

/* ---- 统一输出模式（p9-ui 跨工具一致性约定）---- */
enum mt_output_mode {
	MT_OUT_NORMAL = 0,
	MT_OUT_QUIET,    /* 仅错误/警告 */
	MT_OUT_VERBOSE,  /* 展开细节 */
	MT_OUT_JSON,     /* 结构化输出，一等公民 */
};

extern enum mt_output_mode g_mt_mode;
extern int g_mt_color;   /* 非零启用 ANSI 颜色 */

/* ---- 轻量 UI 辅助（ui.c）---- */
void mt_ui_init(void);                       /* 检测 TTY / NO_COLOR，设 g_mt_color */
const char *mt_c_error(void);               /* 红 ✘ */
const char *mt_c_warn(void);                /* 黄 ⚠ */
const char *mt_c_info(void);                /* 青 ℹ */
const char *mt_c_success(void);             /* 绿 ✔ */
const char *mt_c_dim(void);                 /* 灰（次要信息） */
const char *mt_c_reset(void);
void mt_msg(int level, const char *fmt, ...); /* level: 0 err 1 warn 2 info 3 ok 4 dbg */

/* ---- 子命令入口（各 .c 文件实现）---- */
int cmd_info(int argc, char **argv);
int cmd_headers(int argc, char **argv);
int cmd_deps(int argc, char **argv);
int cmd_strings(int argc, char **argv);
int cmd_which(int argc, char **argv);
int cmd_diff(int argc, char **argv);
int cmd_inspect(int argc, char **argv);

/* ---- 共享 ELF 加载（load.c）---- */
/* 将文件读入内存并解析为 mt_elf64_view。返回 0 成功，-1 失败（错误信息已打印）。
 * 调用者负责 free(*buf)。 */
int mt_load_elf(const char *path, void **buf, size_t *size,
                struct mt_elf64_view *view);

#endif /* MT_INFO_H */
