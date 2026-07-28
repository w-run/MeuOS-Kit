/* mt-info UI 辅助：统一颜色/消息/JSON 模式（p9-ui 跨工具一致性约定）
 *
 * 颜色语义（全局统一，不各自定义）：
 *   红 = 错误，黄 = 警告，青 = 信息，绿 = 成功，灰 = 次要信息
 * 遵循 NO_COLOR 环境变量标准；stdout 非终端时自动禁用颜色。
 * --json 模式下强制无颜色（结构化输出）。
 */
#include "mt-info.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>

enum mt_output_mode g_mt_mode = MT_OUT_NORMAL;
int g_mt_color = 0;

void
mt_ui_init(void)
{
	g_mt_color = (isatty(STDOUT_FILENO) && isatty(STDERR_FILENO)) ? 1 : 0;
	if (getenv("NO_COLOR") != NULL)
		g_mt_color = 0;
	if (g_mt_mode == MT_OUT_JSON)
		g_mt_color = 0;
}

static const char *
color(const char *code)
{
	return g_mt_color ? code : "";
}

const char *mt_c_error(void)   { return color("\033[31m"); }  /* red   */
const char *mt_c_warn(void)    { return color("\033[33m"); }  /* yellow */
const char *mt_c_info(void)    { return color("\033[36m"); }  /* cyan  */
const char *mt_c_success(void) { return color("\033[32m"); }  /* green */
const char *mt_c_dim(void)     { return color("\033[90m"); }  /* grey  */
const char *mt_c_reset(void)   { return color("\033[0m"); }

void
mt_msg(int level, const char *fmt, ...)
{
	int show = 1;
	switch (level) {
	case 0: /* error   */ show = 1; break;
	case 1: /* warn    */ show = 1; break;
	case 2: /* info    */ show = (g_mt_mode != MT_OUT_QUIET); break;
	case 3: /* success */ show = 1; break;
	case 4: /* debug   */ show = (g_mt_mode == MT_OUT_VERBOSE); break;
	}
	if (!show)
		return;
	const char *tag = NULL;
	if (level == 0)      tag = mt_c_error();
	else if (level == 1) tag = mt_c_warn();
	else if (level == 3) tag = mt_c_success();

	va_list ap;
	va_start(ap, fmt);
	if (tag) {
		if (level == 0)      fprintf(stderr, "%s✘%s ", tag, mt_c_reset());
		else if (level == 1) fprintf(stderr, "%s⚠%s ", tag, mt_c_reset());
		else if (level == 3) fprintf(stdout, "%s✔%s ", tag, mt_c_reset());
		vfprintf((level == 3) ? stdout : stderr, fmt, ap);
		fputc('\n', (level == 3) ? stdout : stderr);
	} else {
		vfprintf(stdout, fmt, ap);
		fputc('\n', stdout);
	}
	va_end(ap);
}
