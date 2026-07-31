#include <libgen.h>
#include <string.h>

/*
 * POSIX dirname(): 返回 path 去掉最后一个分量后的目录部分。
 * 输入可被就地修改；对 "." / ".." / 纯斜杠等退化输入返回只读字符串字面量。
 * 边界语义（与 musl 一致）：
 *   dirname("/")    -> "/"      dirname("///")   -> "/"
 *   dirname("/usr") -> "/"      dirname("/usr/") -> "/"
 *   dirname("a/b")  -> "a"      dirname("a/b/")  -> "a"
 *   dirname("a")    -> "."      dirname("./")    -> "."
 *   dirname("..")   -> "."      dirname("")/NULL -> "."
 */
char *
dirname(char *path)
{
	size_t end;
	size_t i;

	if (!path || !*path)
		return ".";
	end = strlen(path);

	/* 跳过尾部斜杠，定位最后一个分量。 */
	i = end;
	while (i > 0 && path[i - 1] == '/')
		--i;
	if (i == 0)
		return "/";
	/* 回退到最后一个分量前的分隔符。 */
	while (i > 0 && path[i - 1] != '/')
		--i;
	if (i == 0)
		return ".";
	/* 再跳过父目录与最后一个分量之间的重复斜杠。 */
	while (i > 1 && path[i - 1] == '/')
		--i;
	path[i] = '\0';
	return path;
}

/*
 * POSIX basename(): 返回 path 的最后一个分量。
 * 对退化输入返回只读字符串字面量：
 *   basename("/")   -> "/"     basename("///")  -> "/"
 *   basename("/usr")-> "usr"   basename("a/b")  -> "b"
 *   basename("a")   -> "a"     basename("")/NULL-> "."
 */
char *
basename(char *path)
{
	size_t end;
	size_t i;

	if (!path || !*path)
		return ".";
	end = strlen(path);
	i = end;
	while (i > 0 && path[i - 1] == '/')
		--i;
	if (i == 0)
		return "/";
	/* 截掉尾部斜杠，使最后一个分量以 NUL 结尾。 */
	path[i] = '\0';
	while (i > 0 && path[i - 1] != '/')
		--i;
	return path + i;
}
