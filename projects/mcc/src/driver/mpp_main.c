/* mpp_main.c — m++ (C++ compiler) driver entry point.
 *
 * The m++ binary shares the mcc driver logic (argv parsing, sysroot setup,
 * backend handoff) via src/driver/main.c's mcc_main(); the difference is
 * the default language: m++ parses C++ source (.cc/.cpp) using the C++
 * frontend (src/cpp/), while mcc parses C.  This is the dual-binary
 * shared-backend design: both drivers link libmcc.a and share the driver
 * machinery, only the frontend dispatch differs.
 *
 * Stage C.1: the C++ frontend is bootstrapped as a C-superset — the C
 * parser is the base grammar, and C++ constructs are layered on top in
 * src/cpp/.  Until the C++ frontend matures, m++ falls back to the C
 * parser for C-compatible input (the C++ lexer/parser are wired in
 * incrementally in C.1.2/C.1.3).
 */
#include <stdio.h>
#include <stdlib.h>

int mcc_main(int, char **);
extern int g_lang;

int
main(int argc, char *argv[])
{
	/* m++ defaults to the C++ frontend.  C++ constructs are layered over
	 * the C parser incrementally (C.1.3+); until then C-compatible input
	 * is parsed by the shared C parser. */
	g_lang = 1;
	return mcc_main(argc, argv);
}
