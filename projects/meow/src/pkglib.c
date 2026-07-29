/* pkglib.c — MeuOS 已知库 flags 数据库
 *
 * 内置查询表，作为 pkg-config 的替代。当 meow 构建外部软件包时，
 * 配方可以通过 `uses:` 语法引用这些库，获取编译/链接参数。
 *
 * 此表会随实际构建经验不断扩充。新增条目只需在此处添加一行。 */

#include "meow.h"
#include <string.h>

const struct pkg_lib known_libs[] = {
	/* 压缩库 */
	{"zlib",       "", "-lz"},
	{"libzstd",    "", "-lzstd"},
	{"liblzma",    "", "-llzma"},
	{"lzma",       "", "-llzma"},
	{"bz2",        "", "-lbz2"},
	{"bzip2",      "", "-lbz2"},

	/* 终端与行编辑 */
	{"ncurses",    "-D_GNU_SOURCE", "-lncurses"},
	{"ncursesw",   "-D_GNU_SOURCE", "-lncursesw"},
	{"readline",   "", "-lreadline -lncurses"},

	/* 加密 */
	{"libcrypto",  "", "-lcrypto"},
	{"libssl",     "", "-lssl"},
	{"openssl",    "", "-lssl -lcrypto"},
	{"libtls",     "", "-ltls -lssl -lcrypto"},

	/* 数学（GCC/自举依赖） */
	{"gmp",        "", "-lgmp"},
	{"mpfr",       "", "-lmpfr -lgmp"},
	{"mpc",        "", "-lmpc -lmpfr -lgmp"},

	/* 正则 */
	{"libpcre2-8", "", "-lpcre2-8"},
	{"pcre2",      "", "-lpcre2-8"},

	/* 系统工具 */
	{"uuid",       "", "-luuid"},
	{"blkid",      "", "-lblkid"},
	{"mount",      "", "-lmount"},

	/* 数据库 */
	{"sqlite3",    "", "-lsqlite3"},
	{"gdbm",       "", "-lgdbm"},

	/* 网络 */
	{"libcurl",    "", "-lcurl"},
	{"fuse3",      "", "-lfuse3"},

	{NULL, NULL, NULL}  /* sentinel */
};

const struct pkg_lib *
find_lib(const char *name)
{
	for (int i = 0; known_libs[i].name; i++)
		if (strcmp(known_libs[i].name, name) == 0)
			return &known_libs[i];
	return NULL;
}
