/* usage.c - --version / --help text for the mcc/m++ driver.
 * 双语：--lang=zh 时 --help/usage 输出中文版（i18n.h 的 g_msg_lang）。 */
#include <stdio.h>
#include <string.h>
#include "driver_internal.h"
#include "i18n.h"

void
print_version(void)
{
	extern char *argv0;
	const char *p = argv0 ? argv0 : "mcc";
	if (strcmp(p, "m++") == 0 || strcmp(p, "mpp") == 0)
		printf("m++ (MeuOS C++ Compiler) %s\n", MCC_VERSION);
	else
		printf("mcc (MeuOS C Compiler) %s\n", MCC_VERSION);
}

void
usage(void)
{
	if (g_msg_lang == 1)
		fprintf(stderr,
"用法: %s [选项] 文件...\n"
"  --help          显示完整帮助\n"
"  --version       打印版本\n",
			argv0);
	else
		fprintf(stderr,
"usage: %s [options] file...\n"
"  --help          show full help\n"
"  --version       print version\n",
			argv0);
	exit(2);
}

static void
usage_long_en(void)
{
	printf(
"usage: %s [options] file...\n"
"\n"
"Output control:\n"
"  -o <file>          output file\n"
"  -c                 compile to .o, do not link\n"
"  -S                 emit assembly only, do not assemble\n"
"  -E                 preprocess only\n"
"\n"
"Preprocessing:\n"
"  -D<name>[=<val>]   define macro\n"
"  -U<name>           undefine macro\n"
"  -I<dir>            add to header search path\n"
"  --sysroot=<dir>    use <dir>/include and <dir>/lib as MeuOS sysroot\n"
"  --specs=meuos      (default) MeuOS specs: -nostdlib -static + meuos crt/libc\n"
"  --meuos            short for --specs=meuos\n"
"  -nostdinc          do not search standard header paths\n"
"  -M                 generate make dependencies to stdout\n"
"  -MM                like -M but ignore system headers\n"
"  -MD <file>         like -M but write to <file>\n"
"  -MMD <file>        like -MM but write to <file>\n"
"  -P                 suppress line markers in -E output\n"
"  -H                 print included files\n"
"\n"
"Linking:\n"
"  -L<dir>            add to library search path\n"
"  -l<lib>            link library\n"
"  -static            static link\n"
"  -shared            generate a shared library\n"
"  -nostdlib          do not link standard libraries\n"
"  -nodefaultlibs     do not link default libraries\n"
"  -pie / -fPIE / -fpic / -fPIC   position-independent code (recorded)\n"
"\n"
"Language:\n"
"  -std=<standard>    select language mode (defines __STDC_VERSION__/__cplusplus):\n"
"                    c89, c99, c11, c17, c23, c++98, c++11, c++14, c++17,\n"
"                    c++20, c++23, and gnu*/iso9899:* aliases\n"
"  -x <lang>          force input language: c or c++ (overrides suffix/driver default)\n"
"  -f<feature>        feature flag (e.g. -fno-omit-frame-pointer, -fPIC, -fPIE);\n"
"                    accepted no-op for other -f/-fno- flags (gcc/clang compat)\n"
"  -ftls-model=<m>    TLS access model: global-dynamic, initial-exec, local-exec\n"
"  --lang=en|zh       diagnostic/help language (default: en, or LANG)\n"
"\n"
"Optimization & diagnostics:\n"
"  -O<level>          optimization: -O0 (none) -O1 (fold/copy) -O2 (default,\n"
"                    fold/copy/gvn/dce) -O3 (+extra fold round) -Os/-Oz\n"
"                    (size-oriented) -Og (debug-friendly, keeps frame\n"
"                    pointer) -Ofast (-O3 + fast-math)\n"
"  -g                 debug info: -g0 (none) -g/-g1/-g2/-g4/-g5 (DWARF\n"
"                    level; -gdwarf[-N] accepted) — emits .debug_* sections\n"
"  -d<flags>          backend debug dumps (A/I/R/S/X)\n"
"  -pg                gprof profiling (accepted, no-op)\n"
"  -w                 suppress warnings\n"
"  -W<warning>        warning control (-Wall, -Werror, -Wno-error, -Wno-all, ...)\n"
"  --color[=auto|always|never]   colored diagnostics (default: auto = tty)\n"
"  --error-json       structured JSON diagnostics\n"
"  --explain          append fix hints\n"
"\n"
"Assembler/linker passthrough:\n"
"  -Wa,<args>         pass comma-separated options to the assembler driver\n"
"  -Wl,<args>         pass comma-separated options to the linker driver\n"
"\n"
"Target:\n"
"  -target <triplet>  target triplet (x86_64, aarch64, riscv64, i386, loongarch64; alias: -t)\n"
"  -m<option>         machine option (e.g. -m64, -march=native)\n"
"\n"
"Other:\n"
"  -v / --verbose     verbose (print executed commands)\n"
"  -pipe              use pipes (recorded)\n"
"  -pedantic          pedantic (recorded)\n"
"  -specs=<file>      specs file (recorded)\n",
		argv0);
}

static void
usage_long_zh(void)
{
	printf(
"用法: %s [选项] 文件...\n"
"\n"
"输出控制:\n"
"  -o <文件>          输出文件\n"
"  -c                 只编译为 .o，不链接\n"
"  -S                 只生成汇编，不汇编\n"
"  -E                 只做预处理\n"
"\n"
"预处理:\n"
"  -D<名>[=<值>]      定义宏\n"
"  -U<名>             取消定义宏\n"
"  -I<目录>           添加头文件搜索路径\n"
"  --sysroot=<目录>   以 <目录>/include 和 <目录>/lib 作为 MeuOS 系统根\n"
"  --specs=meuos      （默认）MeuOS specs: -nostdlib -static + meuos crt/libc\n"
"  --meuos            --specs=meuos 的简写\n"
"  -nostdinc          不搜索标准头文件路径\n"
"  -M                 生成 make 依赖输出到 stdout\n"
"  -MM                同 -M 但忽略系统头文件\n"
"  -MD <文件>         同 -M 但写入 <文件>\n"
"  -MMD <文件>        同 -MM 但写入 <文件>\n"
"  -P                 在 -E 输出中抑制行标记\n"
"  -H                 打印包含的头文件\n"
"\n"
"链接:\n"
"  -L<目录>           添加库搜索路径\n"
"  -l<库>             链接库\n"
"  -static            静态链接\n"
"  -shared            生成共享库\n"
"  -nostdlib          不链接标准库\n"
"  -nodefaultlibs     不链接默认库\n"
"  -pie / -fPIE / -fpic / -fPIC   位置无关代码（记录）\n"
"\n"
"语言:\n"
"  -std=<标准>        c89, c99, c11, c17, gnu11, gnu17, ...\n"
"  -f<特性>           特性开关（如 -fno-common, -fPIC, -fPIE）\n"
"  -ftls-model=<m>    TLS 访问模型: global-dynamic, initial-exec, local-exec\n"
"  --lang=en|zh       诊断/帮助语言（默认: en，或随 LANG）\n"
"\n"
"优化与诊断:\n"
"  -O<级别>           优化级别: -O0（关闭） -O1（fold/copy） -O2（默认，\n"
"                    fold/copy/gvn/dce） -O3（+额外一轮 fold） -Os/-Oz\n"
"                    （尺寸导向） -Og（调试友好，保留帧指针）\n"
"                    -Ofast（-O3 + fast-math）\n"
"  -g                 调试信息（记录，不产出）\n"
"  -d<标志>           后端调试转储（A/I/R/S）\n"
"  -w                 抑制警告\n"
"  -W<警告>           警告控制（-Wall, -Werror, ...）\n"
"  --color[=auto|always|never]   彩色诊断（默认: auto = 终端自动）\n"
"  --error-json       结构化 JSON 诊断\n"
"  --explain          附加修复建议\n"
"\n"
"目标:\n"
"  -target <三元组>   目标三元组（x86_64, aarch64, riscv64, i386, loongarch64; 别名: -t）\n"
"  -m<选项>           机器选项（如 -m64, -march=native）\n"
"\n"
"其他:\n"
"  -v                 详细模式（打印执行的命令）\n"
"  -pipe              使用管道（记录）\n"
"  -pedantic          严格模式（记录）\n"
"  -specs=<文件>      specs 文件（记录）\n",
		argv0);
}

void
usage_long(void)
{
	if (g_msg_lang == 1)
		usage_long_zh();
	else
		usage_long_en();
	exit(0);
}
