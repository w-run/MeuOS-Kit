/* lex_test.c — m++ C++ lexer unit tests (C.1.2).
 *
 * Verifies the C++ keyword classification table and the token kind
 * mapping.  The C lexer remains the base; these tests validate the C++
 * keyword overlay that re-interprets identifiers.
 */
#include <stdio.h>
#include <string.h>

#include "cpp/cpp_tokens.h"

extern enum cpp_tokenkind cpp_classify_ident(const char *, size_t);
extern int cpp_nkeywords(void);

static int failures;

#define CHECK(cond, msg) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); \
		failures++; \
	} \
} while (0)

static void
test_keywords(void)
{
	CHECK(cpp_nkeywords() > 50, "keyword table has C++98/11 core");
	CHECK(cpp_classify_ident("class", 5) == CPP_TCLASS, "class");
	CHECK(cpp_classify_ident("namespace", 9) == CPP_TNAMESPACE, "namespace");
	CHECK(cpp_classify_ident("template", 8) == CPP_TTEMPLATE, "template");
	CHECK(cpp_classify_ident("typename", 8) == CPP_TTYPENAME, "typename");
	CHECK(cpp_classify_ident("this", 4) == CPP_TTHIS, "this");
	CHECK(cpp_classify_ident("new", 3) == CPP_TNEW, "new");
	CHECK(cpp_classify_ident("delete", 6) == CPP_TDELETE, "delete");
	CHECK(cpp_classify_ident("public", 6) == CPP_TPUBLIC, "public");
	CHECK(cpp_classify_ident("private", 7) == CPP_TPRIVATE, "private");
	CHECK(cpp_classify_ident("protected", 9) == CPP_TPROTECTED, "protected");
	CHECK(cpp_classify_ident("friend", 6) == CPP_TFRIEND, "friend");
	CHECK(cpp_classify_ident("virtual", 7) == CPP_TVIRTUAL, "virtual");
	CHECK(cpp_classify_ident("operator", 8) == CPP_TOPERATOR, "operator");
	CHECK(cpp_classify_ident("throw", 5) == CPP_TTHROW, "throw");
	CHECK(cpp_classify_ident("try", 3) == CPP_TTRY, "try");
	CHECK(cpp_classify_ident("catch", 5) == CPP_TCATCH, "catch");
	CHECK(cpp_classify_ident("using", 5) == CPP_TUSING, "using");
	CHECK(cpp_classify_ident("nullptr", 7) == CPP_TNULLPTR, "nullptr");

	/* C++11 */
	CHECK(cpp_classify_ident("decltype", 8) == CPP_TDECLTYPE, "decltype");
	CHECK(cpp_classify_ident("constexpr", 9) == CPP_TCONSTEXPR, "constexpr");
	CHECK(cpp_classify_ident("noexcept", 8) == CPP_TNOEXCEPT, "noexcept");
	CHECK(cpp_classify_ident("static_assert", 13) == CPP_TSTATIC_ASSERT, "static_assert");
	CHECK(cpp_classify_ident("alignof", 7) == CPP_TALIGNOF, "alignof");

	/* C++20 */
	CHECK(cpp_classify_ident("concept", 7) == CPP_TCONCEPT, "concept");
	CHECK(cpp_classify_ident("constinit", 9) == CPP_TCONSTINIT, "constinit");
	CHECK(cpp_classify_ident("requires", 8) == CPP_TREQUIRES, "requires");
	CHECK(cpp_classify_ident("co_await", 8) == CPP_TCO_AWAIT, "co_await");
	CHECK(cpp_classify_ident("module", 6) == CPP_TMODULE, "module");

	/* non-keywords */
	CHECK(cpp_classify_ident("foo", 3) == CPP_TNONE, "foo is ident");
	CHECK(cpp_classify_ident("Class", 5) == CPP_TNONE, "Class is ident");
	CHECK(cpp_classify_ident("my_class", 8) == CPP_TNONE, "my_class ident");
	CHECK(cpp_classify_ident("templatex", 9) == CPP_TNONE, "templatex not keyword");
	CHECK(cpp_classify_ident("", 0) == CPP_TNONE, "empty ident");
}

int
main(void)
{
	test_keywords();

	if (failures) {
		fprintf(stderr, "%d failures\n", failures);
		return 1;
	}
	printf("cpp_lex_test: all checks passed\n");
	return 0;
}
