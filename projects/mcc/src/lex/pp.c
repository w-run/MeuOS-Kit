#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"
#include "mcc.h"
#include "pp_internal.h"

/* Forward declarations for functions defined later */
static FILE *openinclude(const char *, bool, char **);
static char *stripquotes(const char *);

struct macroparam {
	char *name;
	enum {
		PARAMTOK = 1<<0,  /* the parameter is used normally */
		PARAMSTR = 1<<1,  /* the parameter is used with the '#' operator */
		PARAMVAR = 1<<2,  /* the parameter is __VA_ARGS__ */
	} flags;
};

struct macroarg {
	struct token *token;
	size_t ntoken;
	/* stringized argument */
	struct token str;
};

struct macro {
	enum {
		MACROOBJ,
		MACROFUNC,
	} kind;
	char *name;
	/* whether or not this macro is ineligible for expansion */
	bool hide;
	/* parameters of function-like macro */
	struct macroparam *param;
	size_t nparam;
	/* argument tokens of macro invocation */
	struct macroarg *arg;
	/* replacement list */
	struct token *token;
	size_t ntoken;
	/* Per-invocation replacement list for function-like macros.  Keeping it
	 * separate from token[] lets # and ## be resolved before the list enters
	 * the normal expansion stack. */
	struct token *expanded;
	size_t nexpanded;
};

struct frame {
	struct token *token;
	size_t ntoken;
	struct macro *macro;
};

enum ppflags ppflags;

static struct array ctx;
static struct array macros;
/* number of macros currently undergoing expansion */
static size_t macrodepth;

/* include search paths supplied via -I (array of char *) */
static struct array inclpaths;

/* files brought in via #include, tracked for -M/-MD dependency output */
static struct array ppdeps;

/* conditional-compilation stack (#if / #ifdef / #ifndef ...
 * #elif / #else / #endif). Each frame tracks one #if chain. */
struct condframe {
	bool parentactive;  /* was the enclosing context active when entered? */
	bool istaken;        /* is the current branch active? */
	bool anytaken;       /* has any branch in this if/elif chain been taken? */
};
static struct array condstack;

void
ppinit(void)
{
	/* C11 standard predefined macros (required by ISO/IEC 9899:2011 §6.10.8).
	 * Without __STDC__, real-world software using the __P() K&R compatibility
	 * macro (e.g. binutils, gnulib) mis-expands prototypes to empty parameter
	 * lists, causing type-mismatch errors.
	 *
	 * These must be defined BEFORE next() reads the first real token:
	 * ppdefine leaves tok == TEOF after consuming its synthetic input, so
	 * calling it after next() would discard the first token and silently
	 * produce empty output. */
	ppdefine("__STDC__", "1");
	next();
}

static void
macrodel(struct macro *m)
{
	free(m->param);
	free(m->token);
	free(m->expanded);
	free(m);
}

/* check if two macro definitions are equal, as in C11 6.10.3p2 */
static bool
macroequal(struct macro *m1, struct macro *m2)
{
	struct macroparam *p1, *p2;
	struct token *t1, *t2;

	if (m1->kind != m2->kind)
		return false;
	if (m1->kind == MACROFUNC) {
		if (m1->nparam != m2->nparam)
			return false;
		for (p1 = m1->param, p2 = m2->param; p1 < m1->param + m1->nparam; ++p1, ++p2) {
			if (strcmp(p1->name, p2->name) != 0 || p1->flags != p2->flags)
				return false;
		}
	}
	if (m1->ntoken != m2->ntoken)
		return false;
	for (t1 = m1->token, t2 = m2->token; t1 < m1->token + m1->ntoken; ++t1, ++t2) {
		if (t1->kind != t2->kind)
			return false;
		if (t1->lit && strcmp(t1->lit, t2->lit) != 0)
			return false;
	}
	return true;
}

/* find the index of a macro parameter with the given name */
static size_t
macroparam(struct macro *m, struct token *t)
{
	size_t i;

	if (t->kind >= TPPIDENT) {
		for (i = 0; i < m->nparam; ++i) {
			if (strcmp(m->param[i].name, tokenstr(t->kind)) == 0)
				return i;
		}
	}
	return -1;
}

/* lookup a macro by name */
static struct macro *
macroget(enum tokenkind ident)
{
	return arraygetptr(&macros, ident - TPPIDENT);
}

static void
macrodone(struct macro *m)
{
	m->hide = false;
	if (m->kind == MACROFUNC && m->nparam > 0) {
		free(m->arg[0].token);
		free(m->arg);
	}
	free(m->expanded);
	m->expanded = NULL;
	m->nexpanded = 0;
	--macrodepth;
}

static bool
macrovarargs(struct macro *m)
{
	return m->kind == MACROFUNC && m->nparam > 0 && m->param[m->nparam - 1].flags & PARAMVAR;
}

static struct token *
framenext(struct frame *f)
{
	return f->ntoken--, f->token++;
}

/* push a new context frame */
static struct frame *
ctxpush(struct token *t, size_t n, struct macro *m, bool space)
{
	struct frame *f;

	f = arrayadd(&ctx, sizeof(*f));
	f->token = t;
	f->ntoken = n;
	f->macro = m;
	if (n > 0)
		t[0].space = space;
	return f;
}

/* get the next token from the context */
static struct token *
ctxnext(void)
{
	struct frame *f;
	struct token *t;
	struct macro *m;
	bool space;
	size_t i;

again:
	for (f = arraylast(&ctx, sizeof(*f)); ctx.len; --f, ctx.len -= sizeof(*f)) {
		if (f->ntoken)
			break;
		if (f->macro)
			macrodone(f->macro);
	}
	if (ctx.len == 0)
		return NULL;
	m = f->macro;
	if (m && m->kind == MACROFUNC) {
		/* try to expand macro parameter */
		space = f->token->space;
		switch (f->token->kind) {
		case THASH:
			framenext(f);
			t = framenext(f);
			assert(t);
			i = macroparam(m, t);
			assert(i != -1);
			f = ctxpush(&m->arg[i].str, 1, NULL, space);
			break;
		default:
			if (f->token->kind >= TPPIDENT) {
				i = macroparam(m, f->token);
				if (i == -1)
					break;
				framenext(f);
				if (m->arg[i].ntoken == 0)
					goto again;
				f = ctxpush(m->arg[i].token, m->arg[i].ntoken, NULL, space);
			}
			break;
		}
	}
	return framenext(f);
}

static void
define(void)
{
	struct token *t;
	enum tokenkind ident, prev;
	struct macro *m, *other;
	struct macroparam *p;
	struct array params = {0}, repl = {0};
	size_t i;

	m = xmalloc(sizeof(*m));
	m->expanded = NULL;
	m->nexpanded = 0;
	ident = tok.kind;
	m->name = tokencheck(&tok, TPPIDENT, "after #define");
	m->hide = false;
	t = arrayadd(&repl, sizeof(*t));
	scan(t);
	if (t->kind == TLPAREN && !t->space) {
		m->kind = MACROFUNC;
		/* read macro parameter names */
		p = NULL;
		while (scan(&tok), tok.kind != TRPAREN) {
			if (p) {
				if (p->flags & PARAMVAR)
					tokencheck(&tok, TRPAREN, "after '...'");
				tokencheck(&tok, TCOMMA, "or ')' after macro parameter");
				scan(&tok);
			}
			p = arrayadd(&params, sizeof(*p));
			p->flags = 0;
			if (tok.kind == TELLIPSIS) {
				p->name = "__VA_ARGS__";
				p->flags |= PARAMVAR;
			} else {
				p->name = tokencheck(&tok, TPPIDENT, "of macro parameter name or '...'");
			}
		}
		scan(t);  /* first token in replacement list */
	} else {
		m->kind = MACROOBJ;
	}
	m->param = params.val;
	m->nparam = params.len / sizeof(m->param[0]);

	/* read macro body */
	i = macroparam(m, t);
	while (t->kind != TNEWLINE && t->kind != TEOF) {
		prev = t->kind;
		t = arrayadd(&repl, sizeof(*t));
		scan(t);
		if (t->kind == T__VA_ARGS__ && !macrovarargs(m))
			error(&t->loc, "__VA_ARGS__ can only be used in variadic function-like macros");
		if (m->kind != MACROFUNC)
			continue;
		if (i != -1)
			m->param[i].flags |= PARAMTOK;
		i = macroparam(m, t);
		if (prev == THASH) {
			tokencheck(t, TPPIDENT, "after '#' operator");
			if (i == -1)
				error(&t->loc, "'%s' is not a macro parameter name", t->lit);
			m->param[i].flags |= PARAMSTR;
			i = -1;
		}
	}
	m->token = repl.val;
	m->ntoken = repl.len / sizeof(*t) - 1;
	tok = *t;

	i = ident - TPPIDENT;
	other = arraysetptr(&macros, i, m);
	if (other) {
		if (!macroequal(m, other))
			error(&tok.loc, "redefinition of macro '%s'", m->name);
		macrodel(other);
	}
}

static void
undef(void)
{
	enum tokenkind ident;
	struct macro *m;

	ident = tok.kind;
	tokencheck(&tok, TPPIDENT, "after #undef");
	m = macroget(ident);
	if (m) {
		macrodel(m);
		((void **)macros.val)[ident - TPPIDENT] = NULL;
	}
	scan(&tok);
}

/* ----- conditional compilation ----- */

static bool
condactive(void)
{
	struct condframe *f = arraylast(&condstack, sizeof *f);
	return f ? (f->parentactive && f->istaken) : true;
}

static void
pushcond(bool parent, bool val)
{
	struct condframe *f = arrayadd(&condstack, sizeof *f);
	f->parentactive = parent;
	f->istaken = parent && val;
	f->anytaken = f->istaken;
}

/* Advance raw tokens until the end of the current directive line. */
static void
expectnewline(void)
{
	while (tok.kind != TNEWLINE && tok.kind != TEOF)
		scan(&tok);
}

/* The #if constant-expression arithmetic evaluator now lives in
 * pp_expr.c; evalexpr() below calls evalconst() via pp_internal.h. */


/*
 * Read and evaluate a #if / #elif controlling expression. Macro expansion
 * is applied (object-like macros are substituted); the `defined` operator
 * is resolved before expansion; any identifier that remains after expansion
 * is replaced with 0. Leaves tok == TNEWLINE (or TEOF).
 */
static bool
evalexpr(void)
{
	struct array line = {0}, defline = {0}, exp = {0};
	struct token *t;
	enum tokenkind definedtk;
	enum ppflags oldflags;
	size_t i, n;

	/* 1. read the raw expression line (including a TNEWLINE sentinel) */
	for (;;) {
		t = arrayadd(&line, sizeof *t);
		scan(t);
		if (t->kind == TNEWLINE || t->kind == TEOF)
			break;
	}
	n = line.len / sizeof *t;

	/* 2. resolve the `defined` operator (defined X / defined(X)) */
	definedtk = tokenget("defined", 7);
	for (i = 0; i < n; ) {
		t = (struct token *)line.val + i;
		if (t->kind == definedtk) {
			struct token *name;
			size_t nexti = i + 1;
			if (nexti < n && ((struct token *)line.val)[nexti].kind == TLPAREN) {
				if (nexti + 2 >= n)
					error(&t->loc, "malformed defined()");
				name = &((struct token *)line.val)[nexti + 1];
				nexti = nexti + 3;
			} else {
				if (nexti >= n)
					error(&t->loc, "macro name missing after 'defined'");
				name = &((struct token *)line.val)[nexti];
				nexti = nexti + 1;
			}
			{
				struct macro *m = (name->kind >= TPPIDENT) ? macroget(name->kind) : NULL;
				struct token num = { .kind = TNUMBER, .lit = m ? "1" : "0" };
				arrayaddbuf(&defline, &num, sizeof num);
			}
			i = nexti;
		} else {
			arrayaddbuf(&defline, t, sizeof *t);
			++i;
		}
	}

	/* 2.5 resolve __has_include("header") / __has_include(<header>) */
	{
		enum tokenkind hastk = tokenget("__has_include", 13);
		size_t j, newn = defline.len / sizeof(struct token);

		for (j = 0; j < newn; ) {
			t = (struct token *)defline.val + j;
			if (t->kind == hastk) {
				struct token *name;
				bool found = false;
				size_t nextj = j + 1;

				if (nextj < newn && ((struct token *)defline.val)[nextj].kind == TLPAREN) {
					nextj++;
					if (nextj < newn && ((struct token *)defline.val)[nextj].kind == TSTRINGLIT) {
						/* __has_include("header.h") */
						char *hdr = stripquotes(((struct token *)defline.val)[nextj].lit);
						char *path = NULL;
						FILE *f = openinclude(hdr, false, &path);
						found = (f != NULL);
						if (f) fclose(f);
						free(path);
						free(hdr);
						nextj += 2;  /* skip "header" and ) */
					} else if (nextj < newn && ((struct token *)defline.val)[nextj].kind == TLESS) {
						/* __has_include(<header.h>) - scan until > */
						char *hdr = xmalloc(256);
						size_t hlen = 0;

						nextj++;
						while (nextj < newn) {
							struct token *ct = &((struct token *)defline.val)[nextj];
							if (ct->kind == TGREATER)
								break;
							if (hlen + strlen(ct->lit) + 1 < 256) {
								memcpy(hdr + hlen, ct->lit, strlen(ct->lit));
								hlen += strlen(ct->lit);
							}
							nextj++;
						}
						hdr[hlen] = '\0';
						if (nextj < newn && ((struct token *)defline.val)[nextj].kind == TGREATER) {
							char *path = NULL;
							FILE *f = openinclude(hdr, true, &path);
							found = (f != NULL);
							if (f) fclose(f);
							free(path);
							nextj++;
						}
						free(hdr);
					}
					if (nextj < newn && ((struct token *)defline.val)[nextj].kind == TRPAREN)
						nextj++;
				}
				{
					struct token num = { .kind = TNUMBER, .lit = found ? "1" : "0" };
					/* Replace the __has_include(...) sequence with the result token.
					 * We rebuild defline by copying unprocessed tokens. */
					struct array tmp = {0};
					size_t k;
					for (k = 0; k < j; k++)
						arrayaddbuf(&tmp, (struct token *)defline.val + k, sizeof(struct token));
					arrayaddbuf(&tmp, &num, sizeof num);
					for (k = nextj; k < newn; k++)
						arrayaddbuf(&tmp, (struct token *)defline.val + k, sizeof(struct token));
					defline = tmp;
					newn = defline.len / sizeof(struct token);
					j++;  /* advance past the inserted number */
				}
			} else {
				j++;
			}
		}
	}

	/* 3. macro-expand by pushing defline onto the context and pulling
	 *    tokens via next(); PPNEWLINE keeps the trailing sentinel so the
	 *    pull stops without falling through to the main input scanner. */
	ctxpush(defline.val, defline.len / sizeof *t, NULL, false);
	oldflags = ppflags;
	ppflags |= PPNEWLINE;
	for (;;) {
		next();
		if (tok.kind == TNEWLINE || tok.kind == TEOF)
			break;
		arrayaddbuf(&exp, &tok, sizeof tok);
	}
	ppflags = oldflags;

	/* 4. remaining identifiers (not macros) become 0 */
	for (i = 0; i < exp.len / sizeof *t; ++i) {
		t = (struct token *)exp.val + i;
		if (t->kind >= TIDENT) {
			t->kind = TNUMBER;
			t->lit = "0";
		}
	}

	return evalconst(exp.val, exp.len / sizeof *t) != 0;
}

/* Forward declarations for functions called before their definition */
static FILE *openinclude(const char *, bool, char **);
static char *stripquotes(const char *);

/* ----- #include ----- */

static char *
stripquotes(const char *s)
{
	size_t len = strlen(s);
	char *out;

	if (len >= 2 && s[0] == '"' && s[len - 1] == '"') {
		out = xmalloc(len - 1);
		memcpy(out, s + 1, len - 2);
		out[len - 2] = '\0';
	} else {
		out = xmalloc(len + 1);
		memcpy(out, s, len);
		out[len] = '\0';
	}
	return out;
}

/* Search for an include file. For quoted includes the directory of the
 * current file is tried first, then all -I paths. Returns the opened
 * FILE * and stores the resolved full path in *path_out (malloc'd). */
static FILE *
openinclude(const char *name, bool angled, char **path_out)
{
	FILE *f = NULL;
	char *path;
	size_t i, n;

	*path_out = NULL;
	if (!angled) {
		const char *cur = scanfile();
		if (cur) {
			const char *slash = strrchr(cur, '/');
			if (slash) {
				size_t dlen = (size_t)(slash - cur) + 1;
				path = xmalloc(dlen + strlen(name) + 1);
				memcpy(path, cur, dlen);
				strcpy(path + dlen, name);
				f = fopen(path, "r");
				if (f) { *path_out = path; return f; }
				free(path);
			}
		}
	}
	n = inclpaths.len / sizeof(char *);
	for (i = 0; i < n; ++i) {
		const char *dir = ((char **)inclpaths.val)[i];
		size_t dlen = strlen(dir);
		path = xmalloc(dlen + 1 + strlen(name) + 1);
		memcpy(path, dir, dlen);
		path[dlen] = '/';
		strcpy(path + dlen + 1, name);
		f = fopen(path, "r");
		if (f) { *path_out = path; return f; }
		free(path);
	}
	(void)angled;
	return NULL;
}

static void
doinclude(void)
{
	char *name = NULL;
	bool angled = false;
	FILE *f = NULL;
	char *path = NULL;

	scan(&tok);
	if (tok.kind == TSTRINGLIT) {
		angled = false;
		name = stripquotes(tok.lit);
	} else if (tok.kind == TLESS) {
		struct array buf = {0};
		angled = true;
		for (;;) {
			scan(&tok);
			if (tok.kind == TGREATER || tok.kind == TNEWLINE || tok.kind == TEOF)
				break;
			if (tok.space && buf.len)
				arrayaddbuf(&buf, " ", 1);
			{
				const char *s = tok.lit ? tok.lit : tokenstr(tok.kind);
				arrayaddbuf(&buf, s, strlen(s));
			}
		}
		arrayaddbuf(&buf, "", 1);
		name = buf.val;
	} else {
		error(&tok.loc, "expected \"filename\" or <filename> after #include");
	}
	f = openinclude(name, angled, &path);
	if (!f)
		error(&tok.loc, "cannot find include file '%s'", name);
	expectnewline();
	/* Push the included file as a new scanner; on EOF it auto-pops
	 * back to the including file. */
	scanfrom(path, f);
	arrayaddptr(&ppdeps, path);
}

/* Print a Makefile dependency rule "target: input dep1 dep2 ...".
 * Used by the driver to implement -M / -MM / -MD / -MMD. */
void
ppdumpdeps(FILE *f, const char *target, const char *input)
{
	size_t i, n;

	fprintf(f, "%s: %s", target, input ? input : "");
	n = ppdeps.len / sizeof(char *);
	for (i = 0; i < n; ++i)
		fprintf(f, " %s", ((char **)ppdeps.val)[i]);
	fputc('\n', f);
}

/* Skip tokens until a sibling #elif / #else / #endif (at depth 0) is found,
 * processing it. Nested #if..#endif are skipped wholesale via depth
 * counting. Returns with tok == TNEWLINE and either the frame popped
 * (#endif) or a branch now active. Called only from an inactive context. */
static void
skipbody(void)
{
	int depth = 0;
	bool atline = true;

	for (;;) {
		scan(&tok);
		if (tok.kind == TEOF)
			error(&tok.loc, "unterminated conditional directive");
		if (!atline || tok.kind != THASH) {
			atline = (tok.kind == TNEWLINE);
			continue;
		}
		scan(&tok);  /* directive name */
		switch (tok.kind) {
		case TIF:
		case TIFDEF:
		case TIFNDEF:
			++depth;
			expectnewline();
			break;
		case TENDIF:
			if (depth > 0) {
				--depth;
				expectnewline();
				break;
			}
			condstack.len -= sizeof(struct condframe);
			expectnewline();
			return;
	case TELIF:
		if (depth > 0) {
			expectnewline();
			break;
		}
		{
			struct condframe *f = arraylast(&condstack, sizeof *f);
			if (f->parentactive && !f->anytaken) {
				bool val = evalexpr();  /* tok == TNEWLINE */
				f->istaken = val;
				if (val) {
					f->anytaken = true;
					return;  /* now active */
				}
			} else {
				expectnewline();
			}
		}
		break;
	case TELIFDEF:
	case TELIFNDEF:
		if (depth > 0) {
			expectnewline();
			break;
		}
		{
			struct condframe *f = arraylast(&condstack, sizeof *f);
			if (f->parentactive && !f->anytaken) {
				bool is_ndef = (tok.kind == TELIFNDEF);
				scan(&tok);
				bool defined = false;
				if (tok.kind >= TPPIDENT)
					defined = macroget(tok.kind) != NULL;
				bool val = is_ndef ? !defined : defined;
				f->istaken = val;
				if (val) {
					f->anytaken = true;
					return;
				}
			}
			expectnewline();
		}
		break;
		case TELSE:
			if (depth > 0) {
				expectnewline();
				break;
			}
			{
				struct condframe *f = arraylast(&condstack, sizeof *f);
				if (f->parentactive && !f->anytaken) {
					f->istaken = true;
					f->anytaken = true;
					expectnewline();
					return;  /* now active */
				}
				expectnewline();
			}
			break;
		default:
			expectnewline();
			break;
		}
		atline = true;
	}
}

/* ----- command-line macro / include-path API (used by the driver) ----- */

void
ppincludepath(const char *dir)
{
	arrayaddptr(&inclpaths, (void *)dir);
}

void
ppundef(const char *name)
{
	enum tokenkind ident = tokenget(name, strlen(name));
	struct macro *m = macroget(ident);

	if (m) {
		macrodel(m);
		((void **)macros.val)[ident - TPPIDENT] = NULL;
	}
}

/* Define a macro from a -D argument: name is the macro name, value is its
 * replacement text (NULL or "" means gcc's -Dname => "#define name 1"). */
void
ppdefine(const char *name, const char *value)
{
	const char *v = value && *value ? value : "1";
	size_t nl = strlen(name), vl = strlen(v);
	char *buf;
	FILE *f;
	void *cookie;

	buf = xmalloc(nl + 1 + vl + 2);
	memcpy(buf, name, nl);
	buf[nl] = ' ';
	memcpy(buf + nl + 1, v, vl);
	buf[nl + 1 + vl] = '\n';
	buf[nl + 1 + vl + 1] = '\0';

	f = fmemopen(buf, nl + 1 + vl + 1, "r");
	if (!f)
		fatal("fmemopen failed for -D%s", name);
	cookie = scanpushisolated("<command-line>", f);
	scan(&tok);   /* read macro name */
	define();     /* reads body, leaves tok == TNEWLINE */
	while (tok.kind != TEOF)
		scan(&tok);
	scanpopisolated(cookie);
	free(buf);
}

static void
directive(void)
{
	struct location newloc;
	enum ppflags oldflags;
	enum tokenkind kind;

	scan(&tok);
	if (tok.kind == TNEWLINE)
		return;  /* empty directive */
	oldflags = ppflags;
	ppflags |= PPNEWLINE;
	kind = tok.kind;
	switch (kind) {
	case TNUMBER:
		goto line;  /* gcc line markers */
	case TIF: {
		bool parent = condactive(), val = false;
		if (parent)
			val = evalexpr();
		else
			expectnewline();
		pushcond(parent, val);
		if (!condactive())
			skipbody();
		break;
	}
	case TIFDEF: {
		bool parent = condactive(), val = false;
		if (parent) {
			scan(&tok);
			if (tok.kind >= TPPIDENT)
				val = macroget(tok.kind) != NULL;
			expectnewline();
		} else {
			expectnewline();
		}
		pushcond(parent, val);
		if (!condactive())
			skipbody();
		break;
	}
	case TIFNDEF: {
		bool parent = condactive(), val = false;
		if (parent) {
			scan(&tok);
			if (tok.kind >= TPPIDENT)
				val = macroget(tok.kind) == NULL;
			expectnewline();
		} else {
			expectnewline();
		}
		pushcond(parent, val);
		if (!condactive())
			skipbody();
		break;
	}
	case TELIF: {
		struct condframe *f = arraylast(&condstack, sizeof *f);
		if (!f)
			error(&tok.loc, "#elif without #if");
		if (!f->parentactive) {
			f->istaken = false;
			expectnewline();
		} else if (f->anytaken) {
			f->istaken = false;
			expectnewline();
		} else {
			bool val = evalexpr();
			f->istaken = val;
			if (val)
				f->anytaken = true;
		}
		if (!condactive())
			skipbody();
		break;
	}
	case TELIFDEF:
	case TELIFNDEF: {
		struct condframe *f = arraylast(&condstack, sizeof *f);
		if (!f)
			error(&tok.loc, "#elifdef without #if");
		if (!f->parentactive) {
			f->istaken = false;
			expectnewline();
		} else if (f->anytaken) {
			f->istaken = false;
			expectnewline();
		} else {
			bool is_ndef = (tok.kind == TELIFNDEF);
			scan(&tok);
			bool defined = false;
			if (tok.kind >= TPPIDENT)
				defined = macroget(tok.kind) != NULL;
			bool val = is_ndef ? !defined : defined;
			f->istaken = val;
			if (val)
				f->anytaken = true;
			expectnewline();
		}
		if (!condactive())
			skipbody();
		break;
	}
	case TELSE: {
		struct condframe *f = arraylast(&condstack, sizeof *f);
		if (!f)
			error(&tok.loc, "#else without #if");
		if (f) {
			if (!f->parentactive) {
				f->istaken = false;
			} else {
				f->istaken = !f->anytaken;
				f->anytaken = true;
			}
		}
		expectnewline();
		if (!condactive())
			skipbody();
		break;
	}
	case TENDIF: {
		struct condframe *f = arraylast(&condstack, sizeof *f);
		if (!f)
			error(&tok.loc, "#endif without #if");
		condstack.len -= sizeof *f;
		expectnewline();
		break;
	}
	case TINCLUDE:
		if (!condactive())
			expectnewline();
		else
			doinclude();
		break;
	case TEMBED: {
		/* #embed "filename" [limit(N)] [prefix(...)] [suffix(...)] [if_empty(...)]
		 * — embed file as comma-separated byte literal tokens pushed via ctxpush. */
		struct array toks = {0}, prefix_toks = {0}, suffix_toks = {0}, ifempty_toks = {0};
		char *name, *path = NULL;
		FILE *f;
		unsigned char buf[4096];
		size_t nread, total = 0, limit = (size_t)-1;
		long i;
		bool has_ifempty = false;

		if (!condactive()) {
			expectnewline();
			break;
		}
		scan(&tok);
		if (tok.kind != TSTRINGLIT)
			error(&tok.loc, "#embed expects \"filename\"");
		name = stripquotes(tok.lit);
		/* Parse optional parameters: limit(N), prefix(...), suffix(...), if_empty(...) */
		scan(&tok);  /* advance past filename to first param or newline */
		while (tok.kind != TNEWLINE && tok.kind != TEOF) {
			const char *id = (tok.kind >= TPPIDENT || tok.kind == TIDENT)
			                  ? tokenstr(tok.kind) : NULL;
			if (!id) break;

			if (strcmp(id, "limit") == 0) {
				scan(&tok);
				if (tok.kind == TLPAREN) {
					scan(&tok);
					if (tok.kind == TNUMBER)
						limit = strtoul(tok.lit, NULL, 0);
					scan(&tok);
				}
			} else if (strcmp(id, "prefix") == 0 || strcmp(id, "suffix") == 0
			           || strcmp(id, "if_empty") == 0) {
				struct array *buf;
				if (strcmp(id, "prefix") == 0)       buf = &prefix_toks;
				else if (strcmp(id, "suffix") == 0)  buf = &suffix_toks;
				else { buf = &ifempty_toks; has_ifempty = true; }
				scan(&tok);
				if (tok.kind == TLPAREN) {
					int depth = 1;
					scan(&tok);
					while (depth > 0 && tok.kind != TNEWLINE && tok.kind != TEOF) {
						if (tok.kind == TLPAREN) { depth++; }
						else if (tok.kind == TRPAREN) { depth--; if (depth == 0) break; }
						struct token ct = tok;
						if (tok.lit) {
							ct.lit = xmalloc(strlen(tok.lit) + 1);
							strcpy(ct.lit, tok.lit);
						}
						arrayaddbuf(buf, &ct, sizeof ct);
						scan(&tok);
					}
					scan(&tok);
				}
			} else {
				break;
			}
		}
		/* Open and read the file */
		f = openinclude(name, false, &path);
		if (!f)
			error(&tok.loc, "cannot open #embed file '%s'", name);
		while ((nread = fread(buf, 1, sizeof(buf), f)) > 0 && total < limit) {
			size_t nwrite = nread;
			if (total + nwrite > limit)
				nwrite = limit - total;
			for (i = 0; i < (long)nwrite; i++) {
				struct token num;
				char litbuf[8];

				num.kind = TNUMBER;
				num.hide = false;
				num.space = (total + i > 0);
				num.loc = tok.loc;
				snprintf(litbuf, sizeof(litbuf), "%u", buf[i]);
				num.lit = xmalloc(strlen(litbuf) + 1);
				strcpy(num.lit, litbuf);
				arrayaddbuf(&toks, &num, sizeof num);
				if (total + i + 1 < nwrite || total + nwrite < limit) {
					struct token comma = { .kind = TCOMMA, .space = false };
					arrayaddbuf(&toks, &comma, sizeof comma);
				}
			}
			total += nwrite;
		}
		fclose(f);

		/* Assemble: prefix + content (file or if_empty) + suffix.
		 * Each section already carries its own separators from the source,
		 * so we concatenate them directly. */
		{
			struct array final = {0};
			if (prefix_toks.len > 0)
				arrayaddbuf(&final, prefix_toks.val, prefix_toks.len);
			if (toks.len > 0)
				arrayaddbuf(&final, toks.val, toks.len);
			else if (has_ifempty)
				arrayaddbuf(&final, ifempty_toks.val, ifempty_toks.len);
			if (suffix_toks.len > 0)
				arrayaddbuf(&final, suffix_toks.val, suffix_toks.len);
			if (final.len > 0)
				ctxpush(final.val, final.len / sizeof(struct token), NULL, false);
			free(prefix_toks.val); free(suffix_toks.val);
			free(ifempty_toks.val); free(toks.val);
		}
		free(name);
		free(path);
		expectnewline();
		break;
	}
	case TDEFINE:
		scan(&tok);
		define();
		break;
	case TUNDEF:
		scan(&tok);
		undef();
		break;
	case TLINE:
		scan(&tok);
		tokencheck(&tok, TNUMBER, "after #line");
	line:
		newloc.line = strtoull(tok.lit, NULL, 0);
		newloc.col = 1;
		scan(&tok);
		newloc.file = tok.loc.file;
		if (tok.kind == TSTRINGLIT) {
			char *quote;

			/* XXX: handle escape sequences (reuse string decoding from expr.c) */
			quote = strchr(tok.lit, '"');
			assert(quote);
			newloc.file = quote + 1;
			quote = strchr(quote + 1, '"');
			assert(quote);
			*quote = '\0';
			scan(&tok);
		}
		while (tok.kind == TNUMBER)
			scan(&tok);
		scansetloc(newloc);
		break;
	case TERROR:
	case TWARNING:
		diagloc(&tok.loc);
		fprintf(stderr, "%s directive:", kind == TERROR ? "#error" : "#warning");
		scan(&tok);
		while (tok.kind != TNEWLINE && tok.kind != TEOF) {
			tokenprint(&tok, stderr);
			scan(&tok);
		}
		fputc('\n', stderr);
		if (kind == TERROR)
			exit(1);
		break;
	case TPRAGMA:
		while (tok.kind != TNEWLINE && tok.kind != TEOF)
			scan(&tok);
		break;
	default:
		error(&tok.loc, "invalid preprocessor directive #%s", tokenstr(tok.kind));
	}
	tokencheck(&tok, TNEWLINE, "after preprocessing directive");
	ppflags = oldflags;
}

/* get the next token without expanding it */
static void
nextinto(struct token *t)
{
	static bool newline = true;

	for (;;) {
		scan(t);
		if (newline && t->kind == THASH) {
			directive();
			/* After directive returns, if tokens were pushed to context
			 * (e.g. #embed), consume them here instead of scanning source. */
			if (ctx.len > 0) {
				struct token *ct = ctxnext();
				if (ct) {
					*t = *ct;
					break;
				}
			}
		} else {
			newline = tok.kind == TNEWLINE;
			break;
		}
	}
}

static struct token *
rawnext(void)
{
	struct token *t;

	t = ctxnext();
	if (!t) {
		t = &tok;
		nextinto(t);
	}
	return t;
}

static bool
peekparen(void)
{
	static struct array pending;
	struct token *t;
	struct frame *f;

	t = ctxnext();
	if (t) {
		if (t->kind == TLPAREN)
			return true;
		f = arraylast(&ctx, sizeof(*f));
		--f->token;
		++f->ntoken;
		return false;
	}
	pending.len = 0;
	do t = arrayadd(&pending, sizeof(*t)), nextinto(t);
	while (t->kind == TNEWLINE);
	if (t->kind == TLPAREN)
		return true;
	t = pending.val;
	ctxpush(t, pending.len / sizeof(*t), NULL, t[0].space);
	return false;
}

static void
stringize(struct array *buf, struct token *t)
{
	const char *lit;

	if ((t->space || t->kind == TNEWLINE) && buf->len > 1 && ((char *)buf->val)[buf->len - 1] != ' ')
		arrayaddbuf(buf, " ", 1);
	lit = t->lit ? t->lit : tokenstr(t->kind);
	if (t->kind == TSTRINGLIT || t->kind == TCHARCONST) {
		for (; *lit; ++lit) {
			if (*lit == '\\' || *lit == '"')
				arrayaddbuf(buf, "\\", 1);
			arrayaddbuf(buf, lit, 1);
		}
	} else if (lit) {
		arrayaddbuf(buf, lit, strlen(lit));
	}
}

static void expandfunc(struct macro *);

static const char *
tokenspell(const struct token *t)
{
	return t->lit ? t->lit : tokenstr(t->kind);
}

static void
appendarg(struct array *out, struct macro *m, struct token *t)
{
	size_t i, n;

	i = macroparam(m, t);
	if (i == (size_t)-1) {
		arrayaddbuf(out, t, sizeof *t);
		return;
	}
	n = m->arg[i].ntoken;
	for (i = 0; i < n; ++i)
		arrayaddbuf(out, &m->arg[macroparam(m, t)].token[i], sizeof *t);
}

static struct token
paste(struct token left, struct token right)
{
	const char *a = tokenspell(&left);
	const char *b = tokenspell(&right);
	char *joined;
	struct token result;
	size_t alen = strlen(a), blen = strlen(b);

	joined = xmalloc(alen + blen + 1);
	memcpy(joined, a, alen);
	memcpy(joined + alen, b, blen + 1);
	result = left;
	result.kind = tokenget(joined, alen + blen);
	result.lit = NULL;
	result.space = left.space;
	free(joined);
	return result;
}

/* Substitute a function-like macro once, resolving # and ## before its
 * result is returned to the regular expansion stream. */
static void
expandbody(struct macro *m)
{
	struct array out = {0};
	struct token *body;
	size_t i;

	for (i = 0; i < m->ntoken; ++i) {
		struct token current = m->token[i];
		size_t param;

		if (current.kind == THASH) {
			if (++i == m->ntoken)
				error(&current.loc, "missing macro parameter after '#' operator");
			param = macroparam(m, &m->token[i]);
			if (param == (size_t)-1)
				error(&m->token[i].loc, "'%s' is not a macro parameter name",
					tokenspell(&m->token[i]));
			arrayaddbuf(&out, &m->arg[param].str, sizeof current);
			continue;
		}
		if (current.kind == THASHHASH) {
			struct token right;
			struct token *last;

			if (!out.len || ++i == m->ntoken)
				error(&current.loc, "'##' cannot appear at either end of a macro replacement list");
			body = &m->token[i];
			param = macroparam(m, body);
			if (param != (size_t)-1) {
				if (m->arg[param].ntoken != 1)
					error(&body->loc, "'##' requires a single token macro argument");
				right = m->arg[param].token[0];
			} else {
				right = *body;
			}
			last = arraylast(&out, sizeof *last);
			*last = paste(*last, right);
			continue;
		}
		appendarg(&out, m, &current);
	}
	m->expanded = out.val;
	m->nexpanded = out.len / sizeof(struct token);
}

static bool
expand(struct token *t)
{
	struct macro *m;
	bool space;
	static int file_token, line_token;
	char line[32];
	char *literal;
	size_t len;

	if (!file_token) {
		file_token = tokenget("__FILE__", 8);
		line_token = tokenget("__LINE__", 8);
	}
	if (t->kind == file_token) {
		const char *name = t->loc.file ? t->loc.file : "<unknown>";
		len = strlen(name);
		literal = xmalloc(len + 3);
		literal[0] = '"';
		memcpy(literal + 1, name, len);
		literal[len + 1] = '"';
		literal[len + 2] = '\0';
		t->kind = TSTRINGLIT;
		t->lit = literal;
		return false;
	}
	if (t->kind == line_token) {
		snprintf(line, sizeof line, "%zu", t->loc.line);
		len = strlen(line);
		literal = xmalloc(len + 1);
		memcpy(literal, line, len + 1);
		t->kind = TNUMBER;
		t->lit = literal;
		return false;
	}

	if (t->kind < TPPIDENT)
		return false;
	m = macroget(t->kind);
	if (!m || m->hide)
		t->hide = true;
	if (t->hide)
		return false;
	space = t->space;
	if (m->kind == MACROFUNC) {
		if (!peekparen())
			return false;
		expandfunc(m);
		/* A zero-length macro body still needs its cleanup frame below. */
		ctxpush(NULL, 0, m, space);
		ctxpush(m->expanded, m->nexpanded, NULL, space);
		m->hide = true;
		++macrodepth;
		return true;
	}
	ctxpush(m->token, m->ntoken, m, space);
	m->hide = true;
	++macrodepth;
	return true;
}

static void
expandfunc(struct macro *m)
{
	struct macroparam *p;
	struct macroarg *arg;
	struct array str, tok;
	size_t i, depth, paren;
	struct token *t;

	/* read macro arguments */
	paren = 0;
	depth = macrodepth;
	tok = (struct array){0};
	arg = xreallocarray(NULL, m->nparam, sizeof(*arg));
	t = rawnext();
	for (i = 0; i < m->nparam; ++i) {
		p = &m->param[i];
		if (p->flags & PARAMSTR) {
			str = (struct array){0};
			arrayaddbuf(&str, "\"", 1);
		}
		arg[i].ntoken = 0;
		for (;;) {
			if (t->kind == TEOF)
				error(&t->loc, "EOF when reading macro parameters");
			if (macrodepth <= depth) {
				/* adjust current macro depth, in case it got shallower */
				depth = macrodepth;
				if (paren == 0 && (t->kind == TRPAREN || t->kind == TCOMMA && !(p->flags & PARAMVAR)))
					break;
				switch (t->kind) {
				case TLPAREN: ++paren; break;
				case TRPAREN: --paren; break;
				}
				if (p->flags & PARAMSTR)
					stringize(&str, t);
			}
			if (p->flags & PARAMTOK && t->kind != TNEWLINE && !expand(t)) {
				arrayaddbuf(&tok, t, sizeof(*t));
				++arg[i].ntoken;
			}
			t = rawnext();
			while (t->kind == TNEWLINE)
				t = rawnext();
		}
		if (p->flags & PARAMSTR) {
			arrayaddbuf(&str, "\"", 2);
			arg[i].str = (struct token){
				.kind = TSTRINGLIT,
				.lit = str.val,
			};
		}
		if (t->kind == TRPAREN)
			break;
		t = rawnext();
	}
	if (i + 1 < m->nparam)
		error(&t->loc, "not enough arguments for macro '%s'", m->name);
	if (t->kind != TRPAREN)
		error(&t->loc, "too many arguments for macro '%s'", m->name);
	for (i = 0, t = tok.val; i < m->nparam; ++i) {
		arg[i].token = t;
		t += arg[i].ntoken;
	}
	m->arg = arg;
	expandbody(m);
}

void
next(void)
{
	struct token *t;

	do t = rawnext();
	while (expand(t) || t->kind == TNEWLINE && !(ppflags & PPNEWLINE));
	tok = *t;

	/* keyword aliases */
	switch (tok.kind) {
	case ALIAS_ALIGNAS:       tok.kind = TALIGNAS;       break;
	case ALIAS_ALIGNOF:       tok.kind = TALIGNOF;       break;
	case ALIAS_BOOL:          tok.kind = TBOOL;          break;
	case ALIAS__INLINE:
	case ALIAS__INLINE__:     tok.kind = TINLINE;        break;
	case ALIAS__SIGNED:
	case ALIAS__SIGNED__:     tok.kind = TSIGNED;        break;
	case ALIAS_STATIC_ASSERT: tok.kind = TSTATIC_ASSERT; break;
	case ALIAS_THREAD_LOCAL:
	case ALIAS__THREAD:       tok.kind = TTHREAD_LOCAL;  break;
	case ALIAS__TYPEOF:
	case ALIAS__TYPEOF__:     tok.kind = TTYPEOF;        break;
	case ALIAS__VOLATILE__:   tok.kind = TVOLATILE;      break;
	case ALIAS__ASM:          tok.kind = T__ASM__;       break;
	case ALIAS__REAL:         tok.kind = T__REAL__;      break;
	case ALIAS__IMAG:         tok.kind = T__IMAG__;      break;
	case ALIAS__PRAGMA__:     tok.kind = T__PRAGMA__;    break;
	}
}

bool
peek(enum tokenkind kind)
{
	static struct token pending;
	struct token old;

	old = tok;
	next();
	if (tok.kind == kind) {
		next();
		return true;
	}
	pending = tok;
	tok = old;
	ctxpush(&pending, 1, NULL, pending.space);
	return false;
}

char *
expect(enum tokenkind kind, const char *msg)
{
	char *lit;

	lit = tokencheck(&tok, kind, msg);
	next();

	return lit;
}

bool
consume(enum tokenkind kind)
{
	if (tok.kind != kind)
		return false;
	next();
	return true;
}
