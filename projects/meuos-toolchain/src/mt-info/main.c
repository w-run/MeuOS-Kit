/* mt-info - 统一 ELF 分析工具入口
 *
 * 子命令分发 + 统一参数解析（--json/--quiet/--verbose/--no-color）。
 * 各子命令在独立 .c 文件中实现，通过 mt-info.h 的 cmd_* 入口调用。
 */
#include "mt-info.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
usage(FILE *out)
{
	fprintf(out,
"mt-info - MeuOS 统一 ELF 分析工具\n"
"\n"
"用法: mt info|headers|deps|strings|which|diff|inspect <参数...>\n"
"\n"
"子命令:\n"
"  info <elf>          信息卡：类型/架构/入口/节区/符号/依赖一览\n"
"  headers <elf>       快速 ELF / 节区头查看\n"
"  deps <elf>          递归 DT_NEEDED 依赖树\n"
"  strings <elf>       智能字符串提取（去重 / 过滤噪音）\n"
"  which <symbol>      搜索符号：哪个库定义了该符号？\n"
"  diff <a.elf> <b.elf> 语义 diff：节区大小 / 符号增减 / 重定位差异\n"
"  inspect <elf>       TUI 交互式浏览（节区/符号/重定位/依赖/字符串）\n"
"\n"
"通用选项:\n"
"  --json              结构化 JSON 输出（一等公民）\n"
"  --quiet             仅显示错误 / 警告\n"
"  --verbose           展开细节\n"
"  --no-color         禁用 ANSI 颜色（遵循 NO_COLOR 标准）\n"
"  -h, --help          显示此帮助\n"
"  --version           显示版本\n"
"\n"
"示例:\n"
"  mt info /bin/ls\n"
"  mt deps app.elf --json | meow graph --format=mermaid\n"
"  mt which printf\n");
}

/* 剥离全局通用选项，返回剩余参数计数。 */
static int
strip_global_opts(int argc, char **argv)
{
	int out = 0;
	for (int i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--json") == 0) {
			g_mt_mode = MT_OUT_JSON;
		} else if (strcmp(argv[i], "--quiet") == 0) {
			g_mt_mode = MT_OUT_QUIET;
		} else if (strcmp(argv[i], "--verbose") == 0) {
			g_mt_mode = MT_OUT_VERBOSE;
		} else if (strcmp(argv[i], "--no-color") == 0) {
			g_mt_color = 0;
		} else if (strcmp(argv[i], "-h") == 0 ||
		           strcmp(argv[i], "--help") == 0) {
			usage(stdout);
			exit(0);
		} else if (strcmp(argv[i], "--version") == 0) {
			printf("mt-info v%s (MeuOS Toolchain)\n", MT_INFO_VERSION);
			exit(0);
		} else {
			argv[out++] = argv[i];
		}
	}
	return out;
}

int
main(int argc, char **argv)
{
	(void)argc;
	if (argc < 2) {
		usage(stderr);
		return 2;
	}
	/* argv[1] = 子命令；剥离全局选项后重排。 */
	char *cmd = argv[1];
	int rest = strip_global_opts(argc - 2, argv + 2);
	char **rest_argv = argv + 2;

	mt_ui_init();

	if (strcmp(cmd, "info") == 0)     return cmd_info(rest, rest_argv);
	if (strcmp(cmd, "headers") == 0)  return cmd_headers(rest, rest_argv);
	if (strcmp(cmd, "deps") == 0)     return cmd_deps(rest, rest_argv);
	if (strcmp(cmd, "strings") == 0)  return cmd_strings(rest, rest_argv);
	if (strcmp(cmd, "which") == 0)    return cmd_which(rest, rest_argv);
	if (strcmp(cmd, "diff") == 0)     return cmd_diff(rest, rest_argv);
	if (strcmp(cmd, "inspect") == 0)  return cmd_inspect(rest, rest_argv);

	fprintf(stderr, "%s✘%s 未知子命令: %s\n", mt_c_error(), mt_c_reset(), cmd);
	usage(stderr);
	return 2;
}
