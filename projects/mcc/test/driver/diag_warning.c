/* diag_warning.c — 诊断警告测试（p9-ui）。
 *
 * 触发 5 类细粒度警告（默认关闭，-Wall/-Wextra/-Wxxx 显式开启）：
 *   -Wunused-variable   unused_var
 *   -Wunused-parameter  unused_param
 *   -Wconversion        char c = s;（int -> char 隐式截断）
 *   -Wsign-compare      s < u（有符号/无符号比较）
 *   -Wuninitialized     uninit_use（无初始化器、从未赋值的自动变量）
 *
 * 本文件本身是合法程序：无 -W 选项时不应产生任何警告输出。
 * 对应测试脚本 test/driver/diag_warning.sh。 */
static int helper(int unused_param, int used_param)
{
	int unused_var = 1;
	int uninit_use;
	int s = -1;
	unsigned u = 1;
	char c = s;               /* -Wconversion */
	if (s < u)                /* -Wsign-compare */
		return used_param + uninit_use;   /* -Wuninitialized */
	return c;
}

int main(void) { return helper(1, 2); }
