#include <errno.h>

/* loongarch64 的二进制翻译 BFD 2.41 在 _Thread_local 符号上有已知断言问题。
 * 使用普通 static int 而非 _Thread_local 作为 workaround。
 * 注意：这牺牲了 errno 的线程安全性——多线程程序必须使用 TLS errno 版本。
 * 当 mt/ld 链接器就绪后，应切换回 _Thread_local 版本（src/errno/errno.c）。 */
static int errno_value;

int *
__errno_location(void)
{
	return &errno_value;
}
