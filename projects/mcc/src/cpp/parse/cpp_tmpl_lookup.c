/* cpp_tmpl_lookup.c - m++ (C++) template lookup and abbreviation helpers.
 *
 * Stage C.3.2: split from cpp_tmpl_decl.c.  Template name lookup,
 * sizeof... pack query, and abbreviated function template lowering.
 *
 * Cross-file entry points:
 *   cpp_sizeof_pack (expr_unary.c via extern)
 *   cpp_tmpl_find (cpp_tmpl_alias.c, cpp_tmpl_inst.c via extern)
 *   cpp_try_abbrev_decl (cpp_parse.c via extern)
 *   cpp_tmpl_lookup (internal, static)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"
#include "mcc.h"
#include "cpp.h"
int
#include "cpp_internal.h"
#include "../../c/parse/decl_internal.h"
#include "../../c/parse/expr_internal.h"

{
	return g_cpp_pack_depth > 0 ? g_cpp_pack_stack[g_cpp_pack_depth - 1] : 0;
}

/* --- C++11 lambda expressions (anonymous-class lowering) --------------- */

/* Monotonic counter for the synthesized closure class names (`__lambda0`,
 * `__lambda1`, ...). */
int g_cpp_lambda_count;

const char *
cpp_tmpl_lookup(const char *name)
{
	struct cpp_template *t;

	for (t = g_cpp_templates; t; t = t->next)
		if (strcmp(t->name, name) == 0)
			return t->name;
	return NULL;
}

/* Find a template declaration by name (for explicit-argument parsing that
 * needs the parameter kinds). */
struct cpp_template *
cpp_tmpl_find(const char *name)
{
	struct cpp_template *t;

	for (t = g_cpp_templates; t; t = t->next)
		if (strcmp(t->name, name) == 0)
			return t;
	return NULL;
}

/* C++20 abbreviated function templates:
 *   void f(Integral auto x);             -> template<typename __T0>
 *                                           requires Integral<__T0>
 *                                           void f(__T0 x);
 *   auto g(Integral auto x) -> int;      -> ... requires Integral<__T0>
 *                                           auto g(__T0 x) -> int;
 *   void h(SignedIntegral auto... xs);   -> ... requires SignedIntegral<__T0>
 *                                           void h(__T0... xs);
 *   void k(auto x);                      -> template<typename __T0>
 *                                           void k(__T0 x);   (unconstrained)
 *
 * The whole declaration (through ';' or the closing '}' of a function
 * body) is buffered, rewritten into an equivalent template declaration,
 * and replayed through cpp_template_decl.  Returns true when the
 * declaration was an abbreviated function template (handled); false
 * rewinds the stream untouched so the ordinary declaration parser runs. */
bool
cpp_try_abbrev_decl(struct scope *s)
{
	struct token *buf = NULL, *wtoks = NULL;
	size_t bn = 0, cap = 0, wn = 0;
	size_t i, pl = (size_t)-1, ple = (size_t)-1;
	int bd = 0, pd = 0, dd = 0; /* brace/paren/bracket depth */
	int nauto = 0, k;
	struct token cur;
	struct token tpl;
	/* 缓冲循环在 ';' / '}' 处 break 前调用了 next()：该 token（声明
	 * 结束后的下一个）已被消费。not_abbrev 回退时必须把它压回，否则
	 * 输入流永久丢失一个 token，后续声明解析错乱（如跨行
	 * `int\nmain(void)` 报 "expected declaration or function
	 * definition"）。 */
	struct token after;
	bool after_valid = false;

	/* --- buffer the whole declaration --- */
	cur = tok;
	{
		for (;;) {
			if (tok.kind == TEOF)
				break;
			if (bn >= cap) {
				cap = cap ? cap * 2 : 64;
				buf = xreallocarray(buf, cap, sizeof *buf);
			}
			buf[bn++] = tok;
			if (bd == 0 && pd == 0 && dd == 0 && tok.kind == TSEMICOLON) {
				next();
				after = tok;
				after_valid = true;
				break;
			}
			if (tok.kind == TLBRACE)
				++bd;
			else if (tok.kind == TRBRACE) {
				--bd;
				if (bd == 0 && pd == 0 && dd == 0) {
					next();
					after = tok;
					after_valid = true;
					break;
				}
			} else if (tok.kind == TLPAREN)
				++pd;
			else if (tok.kind == TRPAREN)
				--pd;
			else if (tok.kind == TLBRACK)
				++dd;
			else if (tok.kind == TRBRACK)
				--dd;
			next();
		}
	}

	/* --- find the direct function declarator's parameter list: the first
	 * '(' at depth 0 that directly follows an identifier (the function
	 * name) and is not part of a qualified `Class::method` name --- */
	{
		int depth = 0;
		bool prev_id = false, prev_colon2 = false;
		for (i = 0; i < bn; i++) {
			enum tokenkind k2 = buf[i].kind;
			if (k2 == TLPAREN) {
				if (depth == 0 && prev_id && !prev_colon2) {
					pl = i + 1;
					break;
				}
				++depth;
			} else if (k2 == TRPAREN) {
				if (depth > 0)
					--depth;
			} else if (k2 == TLBRACE) {
				++depth;
			} else if (k2 == TRBRACE) {
				if (depth > 0)
					--depth;
			}
			prev_id = k2 >= TIDENT;
			prev_colon2 = k2 == TCOLONCOLON;
		}
	}
	if (pl == (size_t)-1)
		goto not_abbrev;
	/* find the matching ')' */
	{
		int depth = 0;
		for (i = pl; i < bn; i++) {
			if (buf[i].kind == TLPAREN)
				++depth;
			else if (buf[i].kind == TRPAREN) {
				/* depth == 0: this ')' closes the '(' that pl points
				 * past.  (A plain `--depth == 0` test would first
				 * decrement 0 to -1 and never match, so ple would
				 * drift into the function body and count `auto`
				 * locals as template parameters.) */
				if (depth == 0) {
					ple = i;
					break;
				}
				--depth;
			}
		}
	}
	if (ple == (size_t)-1)
		goto not_abbrev;

	/* --- count the `auto` placeholder parameters --- */
	for (i = pl; i < ple; i++)
		if (buf[i].kind == TAUTO)
			++nauto;
	if (!nauto)
		goto not_abbrev;
	if (nauto > 8)
		error_code(E_SYNTAX, &tok.loc,
		    "too many 'auto' parameters in abbreviated function template");

	/* --- rewrite into a template declaration --- */
	tpl = buf[0];
	wtoks = xmalloc((bn + nauto * 24 + 32) * sizeof *wtoks);
	cpp_tb(wtoks, &wn, tpl, 0, "template");
	cpp_tb(wtoks, &wn, tpl, TLESS, NULL);
	for (k = 0; k < nauto; k++) {
		char tn[32];
		if (k)
			cpp_tb(wtoks, &wn, tpl, TCOMMA, NULL);
		cpp_tb(wtoks, &wn, tpl, 0, "typename");
		snprintf(tn, sizeof tn, "__T%d", k);
		cpp_tb(wtoks, &wn, tpl, 0, tn);
	}
	cpp_tb(wtoks, &wn, tpl, TGREATER, NULL);
	/* requires-clause from the constrained parameters */
	{
		bool any = false;
		for (i = pl; i < ple; i++) {
			if (buf[i].kind == TAUTO && i > pl &&
			    buf[i - 1].kind >= TIDENT) {
				struct cpp_template *con;
				for (con = g_cpp_templates; con; con = con->next)
					if (con->is_concept &&
					    strcmp(con->name,
					    tokenstr(buf[i - 1].kind)) == 0) {
						any = true;
						break;
					}
				if (any)
					break;
			}
		}
		if (any) {
			int k2 = 0;
			cpp_tb(wtoks, &wn, tpl, 0, "requires");
			for (i = pl; i < ple; i++) {
				if (buf[i].kind == TAUTO) {
					struct cpp_template *con = NULL;
					if (i > pl && buf[i - 1].kind >= TIDENT) {
						for (con = g_cpp_templates; con; con = con->next)
							if (con->is_concept && strcmp(con->name,
							    tokenstr(buf[i - 1].kind)) == 0)
								break;
					}
					if (con) {
						char tn[32];
						if (k2 > 0)
							cpp_tb(wtoks, &wn, tpl, TLAND, NULL);
						cpp_tb(wtoks, &wn, tpl, 0, con->name);
						cpp_tb(wtoks, &wn, tpl, TLESS, NULL);
						snprintf(tn, sizeof tn, "__T%d", k2);
						cpp_tb(wtoks, &wn, tpl, 0, tn);
						cpp_tb(wtoks, &wn, tpl, TGREATER, NULL);
					}
					++k2;
				}
			}
		}
	}
	/* the rewritten declaration: drop each concept name that precedes a
	 * constrained `auto`, replace every `auto` with its `__Tk` */
	{
		int k2 = 0;
		for (i = 0; i < bn; i++) {
			if (i >= pl && i < ple && buf[i].kind >= TIDENT &&
			    i + 1 < ple && buf[i + 1].kind == TAUTO) {
				struct cpp_template *con;
				for (con = g_cpp_templates; con; con = con->next)
					if (con->is_concept && strcmp(con->name,
					    tokenstr(buf[i].kind)) == 0)
						break;
				if (con)
					continue; /* drop the concept name */
			}
			if (i >= pl && i < ple && buf[i].kind == TAUTO) {
				char tn[32];
				snprintf(tn, sizeof tn, "__T%d", k2++);
				cpp_tb(wtoks, &wn, tpl, 0, tn);
				continue;
			}
			wtoks[wn++] = buf[i];
		}
	}

	/* --- replay `template <...> [requires ...] decl` --- */
	{
		struct token guard = tok; /* token after the declaration */
		tokpush(&guard, 1);
		tokpush(wtoks, wn);
		next();
		cpp_template_decl(s, NULL);
	}
	return true;

not_abbrev:
	/* restore the stream: `tok` is already buf[0]; the remaining
	 * buffered tokens replay from the pushed context.  The buffer loop
	 * consumed one extra token (';' / '}' terminator's successor) with
	 * its final next(); push it back too, or the source stream loses it
	 * and the following declaration misparses. */
	tok = cur;
	if (after_valid)
		tokpush(&after, 1);      /* popped last */
	if (bn > 0)
		tokpush(buf + 1, bn - 1); /* popped first */
	return false;
}

/* Dummy function-pointer type + decl for the template-call placeholder
 * expression (satisfies the TLPAREN "called object" checks until the real
 * instantiation replaces it). */

/* Resolve a specialization explicit-template-argument token name to the
 * concrete type it names (`int` -> &typeint, `long` -> &typelong, a user
 * tag via scopegettag).  Returns NULL when the name is not a recognizable
 * type.  Used by cpp_function_specialization to build the specialization's
 * mangled instantiation key. */
static struct type *
