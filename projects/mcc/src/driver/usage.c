/* usage.c - --version / --help text for the mcc driver. */
#include <stdio.h>
#include "driver_internal.h"

void
print_version(void)
{
	printf("mcc (MeuOS C Compiler) %s\n", MCC_VERSION);
}

void
usage(void)
{
	fprintf(stderr,
"usage: %s [options] file...\n"
"  --help          show full help\n"
"  --version       print version\n",
		argv0);
	exit(2);
}

void
usage_long(void)
{
	printf(
"usage: mcc [options] file...\n"
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
"  -std=<standard>    c89, c99, c11, c17, gnu11, gnu17, ...\n"
"  -f<feature>        feature flag (e.g. -fno-common, -fPIC)\n"
"\n"
"Optimization & diagnostics:\n"
"  -O<level>          optimization (-O0,-O1,-O2,-O3,-Os,-Ofast)\n"
"  -g                 debug info (recorded, not emitted)\n"
"  -d<flags>          backend debug dumps (A/I/R/S)\n"
"  -w                 suppress warnings\n"
"  -W<warning>        warning control (-Wall, -Werror, ...)\n"
"\n"
"Target:\n"
"  -target <triplet>  target triplet (x86_64, aarch64, riscv64, i386, loongarch64; alias: -t)\n"
"  -m<option>         machine option (e.g. -m64, -march=native)\n"
"\n"
"Other:\n"
"  -v                 verbose (print executed commands)\n"
"  -pipe              use pipes (recorded)\n"
"  -pedantic          pedantic (recorded)\n"
"  -specs=<file>      specs file (recorded)\n");
	exit(0);
}
