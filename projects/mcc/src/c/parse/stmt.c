#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"
#include "mcc.h"
#include "cpp.h"

/* C23 labeled break/continue: maps label names to loop/switch targets */
#define MAX_LABELED 64
static struct {
	char *name;
	struct block *break_target;
	struct block *continue_target;
} labeled_blocks[MAX_LABELED];
static int n_labeled_blocks;

static void
register_labeled(const char *name, struct block *brk, struct block *cont)
{
	if (n_labeled_blocks < MAX_LABELED) {
		labeled_blocks[n_labeled_blocks].name = xmalloc(strlen(name) + 1);
		strcpy(labeled_blocks[n_labeled_blocks].name, name);
		labeled_blocks[n_labeled_blocks].break_target = brk;
		labeled_blocks[n_labeled_blocks].continue_target = cont;
		n_labeled_blocks++;
	}
}

static struct block *
find_labeled_break(const char *name)
{
	int i;
	for (i = n_labeled_blocks - 1; i >= 0; i--) {
		if (strcmp(labeled_blocks[i].name, name) == 0)
			return labeled_blocks[i].break_target;
	}
	return NULL;
}

static struct block *
find_labeled_continue(const char *name)
{
	int i;
	for (i = n_labeled_blocks - 1; i >= 0; i--) {
		if (strcmp(labeled_blocks[i].name, name) == 0)
			return labeled_blocks[i].continue_target;
	}
	return NULL;
}

/* Track the label name from label() so break/continue can use it */
static const char *pending_label;

/* 6.8.1 Labeled statements */
static bool
label(struct func *f, struct scope *s)
{
	char *name;
	struct gotolabel *g;
	struct block *b;
	unsigned long long i;

	attr(NULL, ATTRFALLTHROUGH);
	switch (tok.kind) {
	case TCASE:
		next();
		if (!s->switchcases)
			error_code(E_STMT, &tok.loc, "'case' label must be in switch");
		b = mkblock("switch_case");
		funclabel(f, b);
		i = intconstexpr(s, true);
		switchcase(s->switchcases, i, b);
		expect(TCOLON, "after case expression");
		break;
	case TDEFAULT:
		next();
		if (!s->switchcases)
			error_code(E_STMT, &tok.loc, "'default' label must be in switch");
		if (s->switchcases->defaultlabel)
			error_code(E_STMT, &tok.loc, "multiple 'default' labels");
		expect(TCOLON, "after 'default'");
		s->switchcases->defaultlabel = mkblock("switch_default");
		funclabel(f, s->switchcases->defaultlabel);
		break;
	default:
		if (tok.kind < TIDENT)
			return false;
		name = tokenstr(tok.kind);
		if (!peek(TCOLON))
			return false;
		/* C++20 coroutine keywords are keywords, not identifiers, so they
		 * cannot serve as labels.  Refuse to treat `co_return: ...` &
		 * friends as labels; the statement path reports the coroutine
		 * diagnostic instead. */
		{
			extern int g_lang;
			extern enum cpp_tokenkind cpp_tok_kind(void);
			if (g_lang == 1 && (cpp_tok_kind() == CPP_TCO_AWAIT ||
			    cpp_tok_kind() == CPP_TCO_YIELD ||
			    cpp_tok_kind() == CPP_TCO_RETURN))
				return false;
		}
		g = funcgoto(f, name);
		g->defined = true;
		funclabel(f, g->label);
		pending_label = name;
		break;
	}
	return true;
}

static void
labelstmt(struct func *f, struct scope *s)
{
	const char *saved_label = NULL;

	pending_label = NULL;
	while (label(f, s)) {
		/* Last label wins for labeled break/continue */
		if (pending_label)
			saved_label = pending_label;
		pending_label = NULL;
	}
	pending_label = saved_label;
	stmt(f, s);
	pending_label = NULL;
}

/* ---- C++ range-based `for` (`for (auto x : expr) stmt`) -----------------
 * m++ has no native range-for; this lowers it to a traditional for loop
 * over two iterator pointers plus a hidden copy of the range:
 *
 *   for (auto x : arr) BODY
 *     =>
 *   {
 *     auto __c = arr;                          (hidden range copy)
 *     for (auto __b = __c, __e = __c + N;      (array: [&__c[0], &__c[N)))
 *          __b != __e; ++__b) {
 *       auto x = *__b;                         (element binding)
 *       BODY
 *     }
 *   }
 *
 * Member-style ranges (`begin()` / `end()` members) lower identically with
 * `__c.begin()` / `__c.end()` supplying the iterator pair.  The rewritten
 * statement is replayed through the normal for-loop parser, so break /
 * continue / scope / destructor semantics come for free.  The whole thing
 * is a C++ (g_lang == 1) only path; C for-loops are untouched.
 */

/* Grow-only token buffer used while rewriting the range-for statement. */
struct tokbuf {
	struct token *toks;
	size_t n, cap;
};

static void
tb_push(struct tokbuf *b, struct token t)
{
	if (b->n >= b->cap) {
		b->cap = b->cap ? b->cap * 2 : 32;
		b->toks = xreallocarray(b->toks, b->cap, sizeof *b->toks);
	}
	if (t.lit)
		t.lit = strdup(t.lit);
	b->toks[b->n++] = t;
}

static void
tb_punct(struct tokbuf *b, struct location loc, enum tokenkind k)
{
	struct token t;
	memset(&t, 0, sizeof t);
	t.kind = k;
	t.loc = loc;
	tb_push(b, t);
}

static void
tb_ident(struct tokbuf *b, struct location loc, const char *name)
{
	struct token t;
	memset(&t, 0, sizeof t);
	t.kind = tokenget(name, strlen(name));
	t.loc = loc;
	tb_push(b, t);
}

static void
tb_num(struct tokbuf *b, struct location loc, unsigned long long v)
{
	struct token t;
	char buf[32];
	memset(&t, 0, sizeof t);
	snprintf(buf, sizeof buf, "%llu", v);
	t.kind = TNUMBER;
	t.loc = loc;
	t.lit = strdup(buf);
	tb_push(b, t);
}

/* Collect the whole `for (...)` head up to and including its ')' into `h`,
 * leaving the stream just past the ')'.  Records the token index of the
 * range separator ':' (at head depth, not a ternary else) in *colon and,
 * if present, the ';' that terminates an init-statement in *semi.
 * Returns true if the head is a range-for. */
static bool
cpp_head_collect(struct tokbuf *h, int *colon, int *semi)
{
	int depth = 0, q = 0;
	*colon = -1;
	*semi = -1;
	for (;;) {
		if (tok.kind == TEOF)
			error_code(E_SYNTAX, &tok.loc, "unexpected end of file in 'for' header");
		if (depth == 0 && tok.kind == TRPAREN) {
			tb_push(h, tok);
			next();
			break;
		}
		if (depth == 0) {
			if (tok.kind == TQUESTION)
				++q;
			else if (tok.kind == TCOLON) {
				if (q > 0)
					--q;
				else if (*colon < 0)
					*colon = (int)h->n;
			} else if (tok.kind == TSEMICOLON && *semi < 0) {
				*semi = (int)h->n;
			}
		}
		if (tok.kind == TLPAREN || tok.kind == TLBRACK || tok.kind == TLBRACE)
			++depth;
		else if (tok.kind == TRPAREN || tok.kind == TRBRACK ||
		    tok.kind == TRBRACE)
			--depth;
		tb_push(h, tok);
		next();
	}
	return *colon >= 0;
}

/* Scan the `(...)` head of an if/while statement (starting just after
 * the '('), returning true when a ';' appears at head depth — i.e. the
 * head begins with an init-statement (P2360: declaration, expression,
 * or alias) rather than being a bare condition.  Restores the token
 * stream to just after the '(' so the caller re-parses from the start,
 * disambiguating `if (int i = 0; i) ...` from a ctor-call condition
 * `if (Vec(3).f() ...)` without trying decl() first. */
static bool
cpp_if_has_init(void)
{
	struct tokbuf b = {0};
	int depth = 0;
	bool has_semi = false;

	for (;;) {
		if (tok.kind == TEOF)
			error_code(E_STMT, &tok.loc, "unexpected end of file in statement");
		if (depth == 0 && tok.kind == TRPAREN)
			break;
		if (depth == 0 && tok.kind == TSEMICOLON)
			has_semi = true;
		if (tok.kind == TLPAREN || tok.kind == TLBRACK || tok.kind == TLBRACE)
			++depth;
		else if (tok.kind == TRPAREN || tok.kind == TRBRACK || tok.kind == TRBRACE)
			--depth;
		tb_push(&b, tok);
		next();
	}
	tb_push(&b, tok); /* the closing ')' */
	next();
	/* include the token now in `tok` (the one just past ')') so the
	 * replay restores the stream exactly — the scanner is already past
	 * it, mirroring the range-for rewrite */
	tb_push(&b, tok);
	tokpush(b.toks, b.n); /* rewind: parsing resumes at the '(' */
	next();
	return has_semi;
}

static void body_buf_stmt(struct tokbuf *b); /* fwd */

/* Buffer '(' ... matching ')' into `b`. */
static void
body_buf_paren(struct tokbuf *b)
{
	int depth = 0;
	for (;;) {
		if (tok.kind == TEOF)
			error_code(E_STMT, &tok.loc, "unexpected end of file in statement");
		tb_push(b, tok);
		if (tok.kind == TLPAREN)
			++depth;
		else if (tok.kind == TRPAREN && --depth == 0) {
			next();
			return;
		}
		next();
	}
}

/* Buffer one statement (the range-for body) into `b` at the token level.
 * Compound statements brace-match; control statements recurse; anything
 * else is a simple statement ending at the first ';'. */
static void
body_buf_stmt(struct tokbuf *b)
{
	switch (tok.kind) {
	case TLBRACE: {
		int depth = 0;
		for (;;) {
			tb_push(b, tok);
			if (tok.kind == TLBRACE)
				++depth;
			else if (tok.kind == TRBRACE && --depth == 0) {
				next();
				return;
			}
			next();
		}
	}
	case TIF:
		tb_push(b, tok);
		next();
		body_buf_paren(b);
		body_buf_stmt(b);
		if (tok.kind == TELSE) {
			tb_push(b, tok);
			next();
			body_buf_stmt(b);
		}
		return;
	case TWHILE:
	case TFOR:
		tb_push(b, tok);
		next();
		body_buf_paren(b);
		body_buf_stmt(b);
		return;
	case TDO:
		tb_push(b, tok);
		next();
		body_buf_stmt(b);
		tb_push(b, tok); /* 'while' */
		next();
		body_buf_paren(b);
		if (tok.kind == TSEMICOLON) {
			tb_push(b, tok);
			next();
		}
		return;
	case TSWITCH:
		tb_push(b, tok);
		next();
		body_buf_paren(b);
		body_buf_stmt(b);
		return;
	default:
		for (;;) {
			tb_push(b, tok);
			if (tok.kind == TSEMICOLON) {
				next();
				return;
			}
			next();
		}
	}
}

/* Try to parse a C++ range-for starting at the current for-head token
 * (right after the '(' of `for`).  Returns true and consumes the whole
 * statement on success; returns false with the token stream restored if
 * the head is not a range-for. */
static bool
cpp_range_for(struct scope *s, struct func *f)
{
	struct tokbuf h = {0}, out = {0};
	struct expr *re;
	struct type *rt;
	struct location loc = tok.loc;
	bool is_array, has_begin, has_end;
	char mname[128];
	unsigned long long n = 0;
	int colon, semi;
	size_t i;

	if (!cpp_head_collect(&h, &colon, &semi)) {
		/* not a range-for: restore the head for the C for-loop parser.
		 * The head scan read one token past the ')', so push it below
		 * the head tokens to keep the stream position intact, then
		 * reload the first head token into `tok`.  The single past token
		 * must outlive this function (tokpush keeps a pointer), so it
		 * lives on the heap. */
		struct token *past = xmalloc(sizeof *past);
		*past = tok;
		if (past->lit)
			past->lit = strdup(past->lit);
		tokpush(past, 1);
		tokpush(h.toks, h.n);
		next();
		return false;
	}
	/* h.toks = `<head> )`; indices:
	 *   init-statement:  [0 .. semi]  incl. its ';'  (if semi >= 0)
	 *   range decl:      [semi+1 .. colon)           (or [0 .. colon))
	 *   range expression:[colon+1 .. h.n-1)          (exclude the ')')  */

	/* parse the range expression to learn its type.  The current token
	 * (the first token of the loop body) must be preserved: it was read
	 * one past the ')' by the head scan, so push it below the range
	 * expression tokens before re-parsing the range expression. */
	{
		struct token *body_first = xmalloc(sizeof *body_first);
		*body_first = tok;
		if (body_first->lit)
			body_first->lit = strdup(body_first->lit);
		tokpush(body_first, 1);
		tokpush(&h.toks[colon + 1], h.n - 1 - (colon + 1));
	}
	next(); /* load the first range-expression token from the push */
	re = expr(s);
	rt = re->type;
	/* Array ranges arrive decayed to `&arr` (a pointer with the array
	 * type on the operand); recover the array type for the element
	 * count. */
	if (re->kind == EXPRUNARY && re->op == TBAND &&
	    re->base->type->kind == TYPEARRAY)
		rt = re->base->type;
	delexpr(re);

	is_array = rt->kind == TYPEARRAY;
	has_begin = has_end = false;
	if (!is_array && (rt->kind == TYPESTRUCT || rt->kind == TYPEUNION) &&
	    rt->scope) {
		has_begin = cpp_find_unique_member(rt, "begin",
		    mname, sizeof mname) != NULL;
		has_end = cpp_find_unique_member(rt, "end",
		    mname, sizeof mname) != NULL;
	}
	if (!is_array && !(has_begin && has_end))
		error_code(E_CTYPE, &tok.loc, "range-based 'for' requires an array or a type with member 'begin()'/'end()'");
	if (is_array)
		n = rt->size / rt->base->size;

	/* emit the rewritten statement */
	tb_punct(&out, loc, TLBRACE);			/* { */
	if (semi >= 0) {				/* init-statement */
		for (i = 0; i <= (size_t)semi; ++i)
			tb_push(&out, h.toks[i]);
	}
	tb_punct(&out, loc, TAUTO);			/* auto __c = <range>; */
	tb_ident(&out, loc, "__c");
	tb_punct(&out, loc, TASSIGN);
	for (i = (size_t)colon + 1; i + 1 < h.n; ++i)
		tb_push(&out, h.toks[i]);
	tb_punct(&out, loc, TSEMICOLON);

	tb_punct(&out, loc, TFOR);			/* for ( */
	tb_punct(&out, loc, TLPAREN);
	tb_punct(&out, loc, TAUTO);			/* auto __b = <begin>, */
	tb_ident(&out, loc, "__b");
	tb_punct(&out, loc, TASSIGN);
	if (is_array) {
		tb_ident(&out, loc, "__c");
	} else {
		tb_ident(&out, loc, "__c");
		tb_punct(&out, loc, TPERIOD);
		tb_ident(&out, loc, "begin");
		tb_punct(&out, loc, TLPAREN);
		tb_punct(&out, loc, TRPAREN);
	}
	tb_punct(&out, loc, TCOMMA);
	tb_ident(&out, loc, "__e");			/* auto __b, __e = <end>; */
	tb_punct(&out, loc, TASSIGN);
	if (is_array) {
		tb_ident(&out, loc, "__c");
		tb_punct(&out, loc, TADD);
		tb_num(&out, loc, n);
	} else {
		tb_ident(&out, loc, "__c");
		tb_punct(&out, loc, TPERIOD);
		tb_ident(&out, loc, "end");
		tb_punct(&out, loc, TLPAREN);
		tb_punct(&out, loc, TRPAREN);
	}
	tb_punct(&out, loc, TSEMICOLON);		/* __b != __e; */
	tb_ident(&out, loc, "__b");
	tb_punct(&out, loc, TNEQ);
	tb_ident(&out, loc, "__e");
	tb_punct(&out, loc, TSEMICOLON);
	tb_punct(&out, loc, TINC);			/* ++__b) { */
	tb_ident(&out, loc, "__b");
	tb_punct(&out, loc, TRPAREN);
	tb_punct(&out, loc, TLBRACE);

	for (i = semi >= 0 ? (size_t)semi + 1 : 0;	/* <elem-decl> = *__b; */
	    i < (size_t)colon; ++i)
		tb_push(&out, h.toks[i]);
	tb_punct(&out, loc, TASSIGN);
	tb_punct(&out, loc, TMUL);
	tb_ident(&out, loc, "__b");
	tb_punct(&out, loc, TSEMICOLON);

	body_buf_stmt(&out);				/* original body */

	tb_punct(&out, loc, TRBRACE);			/* } } */
	tb_punct(&out, loc, TRBRACE);

	{
		/* Preserve the token after the buffered body: body_buf_stmt
		 * consumed it into `tok`, and the replay below would overwrite
		 * it.  Push it below the rewritten statement tokens (heap copy:
		 * tokpush keeps a pointer). */
		struct token *after_body = xmalloc(sizeof *after_body);
		*after_body = tok;
		if (after_body->lit)
			after_body->lit = strdup(after_body->lit);
		tokpush(after_body, 1);
		tokpush(out.toks, out.n);
	}
	next(); /* load the first rewritten token ('{') */
	stmt(f, s);
	return true;
}

/* ---- C++20 coroutine statements (co_return / co_yield / co_await) -----
 * Phase 1: co_return is lowered as a direct function return (no promise
 * protocol yet; the function is marked iscoroutine for future phases).
 * co_yield and co_await still produce the "not yet supported" diagnostic.
 *
 * The full lowering requires the promise protocol (promise_type with
 * initial_suspend/final_suspend, await_transform, operator new for the
 * coroutine frame) and a suspend-point state machine, none of which m++
 * implements yet beyond the Phase 1 co_return shortcut. */
static void
cpp_coroutine_stmt(struct func *f, struct scope *s, enum cpp_tokenkind ck)
{
	struct token kw = tok;
	struct expr *e = NULL;
	struct value *v = NULL;

	next(); /* consume co_return / co_yield / co_await */

	/* The body contains a coroutine keyword: mark the function as a
	 * coroutine so function-level handling can distinguish it. */
	funcset_iscoroutine(f);

	/* ---- co_return (Phase 1: lowered as a direct return) ------------ */
	if (ck == CPP_TCO_RETURN) {
		if (tok.kind != TSEMICOLON) {
			/* co_return expr ; */
			e = expr(s);
			if (functype(f)->base == &typevoid)
				error_code(E_STMT, &tok.loc,
				    "'co_return' with expression in function returning void");
			/* C++ reference return */
			if (functype(f)->base->isref) {
				extern struct expr *mkunaryexpr(enum tokenkind,
				    struct expr *);
				if (!e->lvalue)
					error_code(E_CTYPE, &tok.loc,
					    "cannot return a non-lvalue as a reference");
				e = mkunaryexpr(TBAND, e);
			}
			e = exprassign(e, functype(f)->base);
			v = funcexpr(f, e);
			delexpr(e);
		}
		/* C++: run destructors before returning */
		{
			extern int g_lang;
			extern void cpp_emit_scope_dtors(struct func *,
			    struct scope *);
			if (g_lang == 1)
				cpp_emit_scope_dtors(f, s);
		}
		funcret(f, v);
		expect(TSEMICOLON, "after 'co_return' statement");
		return;
	}

	/* ---- co_yield / co_await (still unsupported) ------------------- */
	if (tok.kind != TSEMICOLON) {
		e = expr(s);
		delexpr(e);
	}
	expect(TSEMICOLON, ck == CPP_TCO_YIELD ? "after 'co_yield' statement"
	                                       : "after 'co_await' statement");

	error_code(E_STMT, &kw.loc,
	    "C++20 coroutines are not yet supported by m++ (co_%s)",
	    ck == CPP_TCO_YIELD ? "yield" : "await");
}

/* 6.8 Statements and blocks */
void
stmt(struct func *f, struct scope *s)
{
	char *name;
	struct expr *e;
	struct type *t;
	struct value *v;
	struct block *b[4];
	struct switchcases swtch;

	curfunc = f;

	attr(NULL, ATTRFALLTHROUGH);

	/* C++20 coroutine statements: `co_return` / `co_yield` / `co_await`
	 * are C++ keywords, but the C lexer tokenizes them as plain
	 * identifiers.  Reclassify the current token against the C++ keyword
	 * table and route these to the coroutine handler before the C
	 * statement dispatch (which would otherwise treat them as expression
	 * statements and report an undeclared identifier). */
	{
		extern int g_lang;
		if (g_lang == 1 && tok.kind >= TIDENT) {
			enum cpp_tokenkind ck = cpp_tok_kind();
			if (ck == CPP_TCO_RETURN || ck == CPP_TCO_YIELD ||
			    ck == CPP_TCO_AWAIT) {
				cpp_coroutine_stmt(f, s, ck);
				return; /* co_return returns normally (Phase 1); co_yield/co_await are noreturn */
			}
			/* `try { ... } catch (T e) { ... }` — recognised keyword.
			 * The landingpad/unwind backend that routes a thrown
			 * exception to a catch block is not yet implemented, so the
			 * statement is lowered to a clear diagnostic. */
			if (ck == CPP_TTRY) {
				extern void cpp_exc_stmt(struct func *, struct scope *);
				cpp_exc_stmt(f, s);
				return;
			}
		}
	}
	switch (tok.kind) {
	/* 6.8.2 Compound statement */
	case TLBRACE:
		next();
		s = mkscope(s);
		while (tok.kind != TRBRACE) {
			if (!label(f, s) && !decl(s, f))
				stmt(f, s);
		}
		/* record the closing brace so diagnostics for the enclosing
		 * function body can point at `}` instead of the next token */
		funcset_bodyend(f, tok.loc);
		/* C++: run destructors for local class objects on block exit. */
		{
			extern int g_lang;
			extern void cpp_emit_scope_dtors(struct func *,
			    struct scope *);
			if (g_lang == 1)
				cpp_emit_scope_dtors(f, s);
		}
		s = delscope(s);
		next();
		break;

	/* 6.8.3 Expression statement */
	case TSEMICOLON:
		next();
		break;
	default:
		e = expr(s);
		/* [[nodiscard]]: warn when the return of a nodiscard function
		 * is discarded as a statement expression.  The callee of a
		 * direct call is an EXPRIDENT, possibly wrapped in the unary
		 * `&` used to decay the function to a pointer. */
		{
			extern int g_lang;
			struct decl *cd = NULL;
			struct expr *cb = e && e->kind == EXPRCALL ? e->base : NULL;
			if (cb && cb->kind == EXPRUNARY && cb->op == TBAND && cb->base)
				cb = cb->base;
			if (cb && cb->kind == EXPRIDENT && cb->u.ident.decl)
				cd = cb->u.ident.decl;
			if (g_lang == 1 && cd && cd->u.func.isnodiscard)
				/* WARN_RETURN (1<<3) — the return-value warning bucket */
				cc_warn(&tok.loc, 1 << 3,
				    "ignoring return value of nodiscard function '%s'",
				    cd->name);
		}
		v = funcexpr(f, e);
		delexpr(e);
		expect(TSEMICOLON, "after expression statement");
		break;

	/* 6.8.4 Selection statement */
	case TIF: {
		/* C++17 `if constexpr (cond) { ... } else { ... }`: the condition
		 * must be a compile-time constant; the unselected branch is
		 * discarded entirely (skipped at the token level) so templates /
		 * ill-formed code in it are never instantiated. */
		extern int g_lang;
		bool constexpr_if = false;
		bool consteval_if = false;
		bool consteval_neg = false;
		next();
		/* `constexpr` is a C23 keyword (TCONSTEXPR) in the C lexer; the
		 * C++ frontend re-uses that spelling.  `consteval` (C++20) is an
		 * identifier to the C lexer, so it is matched by name. */
		if (g_lang == 1) {
			if (tok.kind == TCONSTEXPR) {
				constexpr_if = true;
				next(); /* consume 'constexpr' */
			} else if (tok.kind >= TIDENT &&
			           strcmp(tokenstr(tok.kind), "consteval") == 0) {
				consteval_if = true;
				next(); /* consume 'consteval' */
			} else if (tok.kind == TLNOT) {
				/* `if ! consteval` — the only valid `!` right after `if` */
				struct token nottok = tok;
				next(); /* consume '!' */
				if (tok.kind >= TIDENT &&
				    strcmp(tokenstr(tok.kind), "consteval") == 0) {
					consteval_if = true;
					consteval_neg = true;
					next(); /* consume 'consteval' */
				} else {
					/* not `if !consteval` (malformed `if !expr`):
					 * rewind so the normal path reports it */
					tokpush(&tok, 1);
					tok = nottok;
				}
			}
		}
		if (consteval_if) {
			extern void cpp_if_consteval(struct func *, struct scope *,
			    bool);
			cpp_if_consteval(f, s, consteval_neg);
			break;
		}
		s = mkscope(s);
		expect(TLPAREN, "after 'if'");
		/* P2360: the head may begin with an init-statement (a declaration
		 * `if (int i = 0; i) ...`, an expression, or a C++ alias routed
		 * through decl()), separated from the condition by ';'.  Scan
		 * ahead for a depth-0 ';' to disambiguate from a ctor-call
		 * condition like `if (Vec(3).f() ...)`, then rewind and parse. */
		if (cpp_if_has_init()) {
			if (!decl(s, f)) {
				if (tok.kind != TSEMICOLON) {
					e = expr(s);
					funcexpr(f, e);
					delexpr(e);
				}
				expect(TSEMICOLON, NULL);
			}
			e = expr(s); /* the condition */
		} else {
			e = expr(s); /* the condition */
		}
		t = e->type;
		if (!(t->prop & PROPSCALAR))
			error_code(E_STMT, &tok.loc, "controlling expression of if statement must have scalar type");
		if (constexpr_if) {
			extern void cpp_if_constexpr(struct func *, struct expr *,
			    struct scope *);
			cpp_if_constexpr(f, e, s);
			s = delscope(s);
			break;
		}
		b[0] = mkblock("if_true");
		b[1] = mkblock("if_false");
		funcbranch(f, e, b[0], b[1]);
		delexpr(e);
		expect(TRPAREN, "after expression");

		funclabel(f, b[0]);
		s = mkscope(s);
		labelstmt(f, s);
		s = delscope(s);

		if (consume(TELSE)) {
			b[2] = mkblock("if_join");
			funcjmp(f, b[2]);
			funclabel(f, b[1]);
			s = mkscope(s);
			labelstmt(f, s);
			s = delscope(s);
			funclabel(f, b[2]);
		} else {
			funclabel(f, b[1]);
		}
		s = delscope(s);
		break;
	}
	case TSWITCH:
		next();

		s = mkscope(s);
		expect(TLPAREN, "after 'switch'");
		e = expr(s);
		expect(TRPAREN, "after expression");

		if (!(e->type->prop & PROPINT))
			error_code(E_STMT, &tok.loc, "controlling expression of switch statement must have integer type");
		e = exprpromote(e);

		swtch.root = NULL;
		swtch.type = e->type;
		swtch.defaultlabel = NULL;

		b[0] = mkblock("switch_cond");
		b[1] = mkblock("switch_join");

		v = funcexpr(f, e);
		funcjmp(f, b[0]);
		s = mkscope(s);
		if (pending_label) {
			register_labeled(pending_label, b[1], NULL);
			pending_label = NULL;
		}
		s->breaklabel = b[1];
		s->switchcases = &swtch;
		labelstmt(f, s);
		funcjmp(f, b[1]);

		funclabel(f, b[0]);
		funcswitch(f, v, &swtch, swtch.defaultlabel ? swtch.defaultlabel : b[1]);
		s = delscope(s);

		funclabel(f, b[1]);
		s = delscope(s);
		break;

	/* 6.8.5 Iteration statements */
	case TWHILE:
		next();
		s = mkscope(s);
		expect(TLPAREN, "after 'while'");
		e = expr(s);
		t = e->type;
		if (!(t->prop & PROPSCALAR))
			error_code(E_CTYPE, &tok.loc, "controlling expression of loop must have scalar type");
		expect(TRPAREN, "after expression");

		b[0] = mkblock("while_cond");
		b[1] = mkblock("while_body");
		b[2] = mkblock("while_join");

		funclabel(f, b[0]);
		funcbranch(f, e, b[1], b[2]);
		funclabel(f, b[1]);
		s = mkscope(s);
		if (pending_label) {
			register_labeled(pending_label, b[2], b[0]);
			pending_label = NULL;
		}
		s->continuelabel = b[0];
		s->breaklabel = b[2];
		labelstmt(f, s);
		s = delscope(s);
		funcjmp(f, b[0]);
		funclabel(f, b[2]);
		s = delscope(s);
		break;
	case TDO:
		next();

		b[0] = mkblock("do_body");
		b[1] = mkblock("do_cond");
		b[2] = mkblock("do_join");

		s = mkscope(s);
		s = mkscope(s);
		if (pending_label) {
			register_labeled(pending_label, b[2], b[1]);
			pending_label = NULL;
		}
		s->continuelabel = b[1];
		s->breaklabel = b[2];
		funclabel(f, b[0]);
		labelstmt(f, s);
		s = delscope(s);

		expect(TWHILE, "after 'do' statement");
		expect(TLPAREN, "after 'while'");
		funclabel(f, b[1]);
		e = expr(s);
		t = e->type;
		if (!(t->prop & PROPSCALAR))
			error_code(E_CTYPE, &tok.loc, "controlling expression of loop must have scalar type");
		expect(TRPAREN, "after expression");

		funcbranch(f, e, b[0], b[2]);
		funclabel(f, b[2]);
		s = delscope(s);
		expect(TSEMICOLON, "after 'do' statement");
		break;
	case TFOR:
		next();
		expect(TLPAREN, "after 'for'");
		s = mkscope(s);
		{
			extern int g_lang;
			if (g_lang == 1 && cpp_range_for(s, f)) {
				s = delscope(s);
				break;
			}
		}
		if (!decl(s, f)) {
			if (tok.kind != TSEMICOLON) {
				e = expr(s);
				funcexpr(f, e);
				delexpr(e);
			}
			expect(TSEMICOLON, NULL);
		}

		b[0] = mkblock("for_cond");
		b[1] = mkblock("for_body");
		b[2] = mkblock("for_cont");
		b[3] = mkblock("for_join");

		funclabel(f, b[0]);
		if (tok.kind != TSEMICOLON) {
			e = expr(s);
			t = e->type;
			if (!(t->prop & PROPSCALAR))
				error_code(E_CTYPE, &tok.loc, "controlling expression of loop must have scalar type");
			funcbranch(f, e, b[1], b[3]);
			delexpr(e);
		}
		expect(TSEMICOLON, NULL);
		e = tok.kind == TRPAREN ? NULL : expr(s);
		expect(TRPAREN, NULL);

		funclabel(f, b[1]);
		s = mkscope(s);
		if (pending_label) {
			register_labeled(pending_label, b[3], b[2]);
			pending_label = NULL;
		}
		s->breaklabel = b[3];
		s->continuelabel = b[2];
		labelstmt(f, s);
		s = delscope(s);

		funclabel(f, b[2]);
		if (e) {
			funcexpr(f, e);
			delexpr(e);
		}
		funcjmp(f, b[0]);
		funclabel(f, b[3]);
		s = delscope(s);
		break;

	/* 6.8.6 Jump statements */
	case TGOTO:
		next();
		name = expect(TIDENT, "after 'goto'");
		funcjmp(f, funcgoto(f, name)->label);
		expect(TSEMICOLON, "after 'goto' statement");
		break;
	case TCONTINUE:
		next();
		if (tok.kind >= TIDENT) {
			/* C23: continue LABEL */
			name = tokenstr(tok.kind);
			{
				struct block *target = find_labeled_continue(name);
				if (!target)
					error_code(E_STMT, &tok.loc, "label '%s' for continue not found", name);
				funcjmp(f, target);
			}
			next();
		} else {
			if (!s->continuelabel)
				error_code(E_STMT, &tok.loc, "'continue' statement must be in loop");
			funcjmp(f, s->continuelabel);
		}
		expect(TSEMICOLON, "after 'continue' statement");
		break;
	case TBREAK:
		next();
		if (tok.kind >= TIDENT) {
			/* C23: break LABEL */
			name = tokenstr(tok.kind);
			{
				struct block *target = find_labeled_break(name);
				if (!target)
					error_code(E_STMT, &tok.loc, "label '%s' for break not found", name);
				funcjmp(f, target);
			}
			next();
		} else {
			if (!s->breaklabel)
				error_code(E_STMT, &tok.loc, "'break' statement must be in loop or switch");
			funcjmp(f, s->breaklabel);
		}
		expect(TSEMICOLON, "after 'break' statement");
		break;
	case TRETURN:
		next();
		t = functype(f);
		if (t->base != &typevoid) {
			/* C++14 `auto` return type: the declared return type is the
			 * `auto` placeholder (&typeauto); deduce it from the return
			 * expression (backfilled by the decl/method-body parser
			 * after the body is parsed). */
			if (t->base == &typeauto) {
				extern int g_lang;
				extern void cpp_auto_return(struct func *, struct expr *);
				e = expr(s);
				if (g_lang == 1)
					cpp_auto_return(f, e);
				v = funcexpr(f, e);
				delexpr(e);
			} else {
				/* C++11 implicit braced-init-list return:
				 * `T f() { return {a, b, ...}; }` — the braced list
				 * initialises a temporary of the function's return
				 * type via its constructor/aggregate-initialisation.
				 * `return Vec{...}` (explicit) already works through the
				 * normal expression path; here `{` begins the list
				 * directly, so we build the temporary explicitly. */
				extern int g_lang;
				if (g_lang == 1 && tok.kind == TLBRACE) {
					extern struct expr *cpp_temp_construct_braced(
					    struct scope *, struct type *);
					e = cpp_temp_construct_braced(s, t->base);
					if (!e)
						error_code(E_CTYPE, &tok.loc, "cannot braced-initialise a return value of this type");
				} else {
					e = expr(s);
				}
				/* C++ reference return: the expression must be an lvalue
				 * and is returned by address (references are hidden
				 * pointers).  Without this `T &f() { return lvalue; }`
				 * hits a pointer-assignment type error. */
				if (t->base->isref) {
					extern struct expr *mkunaryexpr(enum tokenkind,
					    struct expr *);
					if (!e->lvalue)
						error_code(E_CTYPE, &tok.loc, "cannot return a non-lvalue as a reference");
					e = mkunaryexpr(TBAND, e);
				}
				e = exprassign(e, t->base);
				v = funcexpr(f, e);
				delexpr(e);
			}
		} else {
			v = NULL;
		}
		/* C++: run destructors of this scope's local class objects before
		 * returning (they are marked dtor_done so the block-exit sweep
		 * does not run them again). */
		{
			extern int g_lang;
			extern void cpp_emit_scope_dtors(struct func *,
			    struct scope *);
			if (g_lang == 1)
				cpp_emit_scope_dtors(f, s);
		}
		funcret(f, v);
		expect(TSEMICOLON, "after 'return' statement");
		break;

	case T__ASM__: {
		/* Basic inline asm — no-op that compiles through.
		 * Supports __asm__ [volatile]("instructions" [: [outputs] [: [inputs] [: clobbers]]]) */
		next();
		if (tok.kind == TVOLATILE || tok.kind == TCONST || tok.kind == TRESTRICT)
			next();
		if (tok.kind == TLPAREN) {
			next();
			while (tok.kind == TSTRINGLIT) {
				next();
				if (tok.kind == TCOLON) {
					while (tok.kind != TRPAREN && tok.kind != TNEWLINE && tok.kind != TEOF) {
						if (tok.kind == TLPAREN) {
							int depth = 1;
							while (depth > 0 && tok.kind != TEOF) {
								if (tok.kind == TLPAREN) depth++;
								if (tok.kind == TRPAREN) depth--;
								if (depth > 0) next();
							}
						}
						if (tok.kind != TRPAREN) next();
					}
				}
				if (tok.kind == TCOMMA) next();
			}
			expect(TRPAREN, "after asm statement");
		}
		expect(TSEMICOLON, "after asm statement");
		break;
	}
	}
}
