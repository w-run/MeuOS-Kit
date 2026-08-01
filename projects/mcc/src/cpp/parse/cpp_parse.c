/* cpp_parse.c — m++ (C++) parser entry.
 *
 * Stage C.1.3: the C++ parser entry point.  C++ is parsed as a C
 * superset — the C parser (src/parse/) handles C-compatible constructs,
 * and C++-only constructs (class/namespace/template/...) are layered on
 * top here.  The translation-unit loop calls this instead of the C
 * `decl()` loop when the input language is C++ (selected by the m++
 * driver).
 *
 * Currently: `class` declarations with access-control sections
 * (public:/private:/protected:) are handled by cpp_class_decl, which
 * reuses the C type machinery (mktype + addmember + structdecl) while
 * skipping access labels.  Member functions, inheritance, templates,
 * and namespaces are added incrementally in later stages.
 */
#include <stdio.h>
#include <string.h>

#include "util.h"
#include "mcc.h"
#include "cpp.h"
#include "../../parse/decl_internal.h"

/* Current class tag being parsed (set by cpp_class_decl), used to mangle
 * member-function names as ClassName_method. */
static const char *cpp_current_class;
void cpp_define_method(struct scope *s, struct type *funct,
                              const char *mname);

/* Classify the current token as a C++ keyword, if any.  Wired to the C++
 * lexer's keyword table; the C lexer tokenizes identifiers, and this
 * re-interprets them as C++ keywords for the parser.  Identifier names
 * are recovered via tokenstr() (the C lexer stores identifier text in the
 * tokstr table, not in tok.lit). */
enum cpp_tokenkind
cpp_tok_kind(void)
{
	if (tok.kind >= TIDENT) {
		const char *name = tokenstr(tok.kind);
		return cpp_classify_ident(name, name ? strlen(name) : 0);
	}
	return CPP_TNONE;
}

/* Parse a C++ `class`/`struct`/`union` declaration with access-control
 * sections (public:/private:/protected:).  Reuses the C type machinery
 * (mktype + addmember + structdecl) but skips access-specifier labels,
 * which the C parser does not understand.  Currently handles the
 * data-member subset; member functions, inheritance, and templates are
 * added later. */
static bool
cpp_class_decl(struct scope *s)
{
	struct type *t;
	char *tag;
	struct structbuilder b;

	/* class/struct/union keyword consumed here */
	next();
	if (tok.kind < TIDENT)
		error(&tok.loc, "expected class name");
	tag = tokenstr(tok.kind);
	next();
	cpp_current_class = tag; /* for member-function name mangling */

	/* create or look up the aggregate type */
	t = scopegettag(s, tag, tok.kind != TLBRACE && tok.kind != TSEMICOLON);
	if (t) {
		if (t->kind != TYPESTRUCT && t->kind != TYPEUNION)
			error(&tok.loc, "redeclaration of tag '%s' with different kind", tag);
	} else {
		t = mktype(TYPESTRUCT, 0);
		t->size = 0;
		t->align = 0;
		t->u.structunion.tag = tag;
		t->u.structunion.members = NULL;
		t->incomplete = true;
		scopeputtag(s, tag, t);
	}

	if (tok.kind != TLBRACE)
		return true; /* forward declaration */
	if (!t->incomplete)
		error(&tok.loc, "redefinition of class '%s'", tag);
	next(); /* consume '{' */

	b.type = t;
	b.last = &t->u.structunion.members;
	b.bits = 0;
	b.pack = false;

	for (;;) {
		/* skip access-control labels: public: private: protected: */
		enum cpp_tokenkind k = cpp_tok_kind();
		if (k == CPP_TPUBLIC || k == CPP_TPRIVATE || k == CPP_TPROTECTED) {
			next(); /* consume the keyword */
			if (tok.kind == TCOLON)
				next(); /* consume ':' */
			continue;
		}
		if (tok.kind == TCOLON) {
			/* stray colon */
			next();
			continue;
		}
		if (tok.kind == TRBRACE)
			break;
		structdecl(s, &b);
	}
	next(); /* consume '}' */

	/* Finalize: align the aggregate size up to its member alignment and
	 * mark it complete, mirroring tagspec()'s struct branch. */
	if (t->align < 0)
		t->align = 0;
	if (t->size)
		t->size = ALIGNUP(t->size, t->align);
	t->incomplete = false;

	/* trailing ';' after the class body */
	if (tok.kind == TSEMICOLON)
		next();
	return true;
}

/* Parse a C++ translation unit: top-level declaration loop.
 * C++ grammar is layered over the C parser; `class` declarations with
 * access control are handled here (cpp_class_decl), and C-compatible
 * declarations fall through to the shared C parser. */
void
cpp_parse_translation_unit(void)
{
	extern struct scope filescope;
	extern void emittentativedefns(void);

	while (tok.kind != TEOF) {
		/* C++ class/struct/union with access control */
		enum cpp_tokenkind k = cpp_tok_kind();
		if (k == CPP_TCLASS) {
			cpp_class_decl(&filescope);
			continue;
		}

		if (!decl(&filescope, NULL)) {
			if (tok.kind == TSEMICOLON)
				error(&tok.loc, "unexpected ';' at top-level");
			error(&tok.loc, "expected declaration or function definition");
		}
	}
	emittentativedefns();
}

/* --- member function lowering (C.2.3) -------------------------------- */

/* Define a member function as an out-of-line free function named
 * `ClassName_method`.  Reuses the C function-definition machinery
 * (mkdecl/mkfunc/stmt) via a small clone of decl()'s DECLFUNC path.
 * The implicit `this` parameter and in-body member access (`count` ->
 * this->count) are added in the next stage; for now the function body is
 * parsed with no implicit this, so member access inside the body will
 * fail to resolve until that stage lands. */
void
cpp_define_method(struct scope *s, struct type *funct, const char *mname)
{
	extern struct decl *mkdecl(char *, enum declkind, struct type *,
	    enum typequal, enum linkage);

	char mangled[256];
	struct decl *d;

	(void)s;
	if (!cpp_current_class || !mname)
		return;
	snprintf(mangled, sizeof mangled, "%s_%s", cpp_current_class, mname);

	d = mkdecl(mangled, DECLFUNC, funct, QUALNONE, LINKEXTERN);
	d->value = mkglobal(d);
	if (tok.kind != TLBRACE) {
		if (tok.kind == TSEMICOLON)
			next();
		return; /* declaration only */
	}
	/* Stage C.2.3: the member function is lowered to an out-of-line
	 * `Class_method` symbol; this-pointer lowering and in-body member
	 * access are implemented next stage, so the body is consumed without
	 * parsing. */
	if (tok.kind == TLBRACE) {
		int bd = 1;
		next();
		while (bd && tok.kind != TEOF) {
			if (tok.kind == TLBRACE) bd++;
			else if (tok.kind == TRBRACE) bd--;
			next();
		}
	} else if (tok.kind == TSEMICOLON) {
		next();
	}
	d->defined = true;
}
