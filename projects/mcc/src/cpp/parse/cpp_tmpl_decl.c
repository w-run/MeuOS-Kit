/* cpp_tmpl_decl.c - m++ (C++) function/class template declaration.
 *
 * Parses `template <...>` headers and the declaration that follows
 * (cpp_template_decl), plus template-registry lookup helpers
 * (cpp_tmpl_lookup / cpp_tmpl_find / cpp_try_abbrev_decl /
 * cpp_sizeof_pack / tmpl_param_is_nttp).  Extracted from cpp_parse.c.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"
#include "mcc.h"
#include "cpp.h"
#include "cpp_internal.h"
#include "../../c/parse/decl_internal.h"
#include "../../c/parse/expr_internal.h"


int
cpp_sizeof_pack(void)
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

/* Parse `template < typename T, class U, ... >` and buffer the following
 * declaration.  Nothing is defined yet; instantiation happens on first
 * use with concrete type arguments.  `owner` is the enclosing class for
 * a member template (`template<...> T get() {...}` inside a class body),
 * or NULL for a file-scope function/class template. */
void
cpp_template_decl(struct scope *s, struct type *owner)
{
	struct cpp_template *tmpl;
	struct cpp_tmpl_param *p, **pe;
	struct token *toks;
	size_t ntoks = 0, cap = 0;
	int bd = 0;
	bool param;
	/* constraints accumulated from `template<Concept T>` type parameters;
	 * merged with an explicit requires-clause (if any) below */
	struct token *pctoks = NULL;
	size_t pcn = 0, pccap = 0;


	next(); /* consume 'template' */
	expect(TLESS, "after 'template'");
	tmpl = xmalloc(sizeof(*tmpl));
	tmpl->name = NULL;
	tmpl->nparams = 0;
	tmpl->params = NULL;
	tmpl->toks = NULL;
	tmpl->ntoks = 0;
	tmpl->is_class = false;
	tmpl->is_member = owner != NULL;
	tmpl->owner = owner;
	tmpl->insts = NULL;
	tmpl->insts_end = &tmpl->insts;
	tmpl->cls_insts = NULL;
	tmpl->cls_insts_end = &tmpl->cls_insts;
	tmpl->constraint = NULL;
	tmpl->nconstraint = 0;
	tmpl->next = NULL;
	pe = &tmpl->params;

	/* template parameter list: `typename T` / `class T` (type parameter),
	 * `int N` / `auto N` (non-type template parameter, C++17/C++20),
	 * comma separated; a trailing parameter pack: `typename... Args` /
	 * `class... Args` */
	/* Parameter-list scope: type parameters (`T`) are visible as DECLTYPE
	 * decls so a later non-type parameter can name them (`template<typename T,
	 * T N>` — P0847-dependent-type NTTP). */
	{
		extern struct scope *mkscope(struct scope *);
		extern void scopeputdecl(struct scope *, struct decl *);
		struct scope *ps = mkscope(s);
		for (;;) {
			enum cpp_tokenkind k = cpp_tok_kind();
			if (k == CPP_TTYPENAME || k == CPP_TCLASS) {
				next(); /* consume typename/class */
				p = xmalloc(sizeof(*p));
				p->is_pack = false;
				p->is_dep_nttp = false;
				p->is_nttp = false;
				p->nttp_type = NULL;
				if (tok.kind == TELLIPSIS) {
					p->is_pack = true;
					next();
				}
				if (tok.kind < TIDENT)
					error_code(E_SYNTAX, &tok.loc, "expected template parameter name");
				p->name = xmalloc(strlen(tokenstr(tok.kind)) + 1);
				strcpy((char *)p->name, tokenstr(tok.kind));
				p->next = NULL;
				*pe = p;
				pe = &p->next;
				++tmpl->nparams;
				/* a non-pack type parameter is nameable as the type of a
				 * later NTTP; put it in the parameter-list scope */
				if (!p->is_pack) {
					extern struct decl *mkdecl(char *, enum declkind,
					    struct type *, enum typequal, enum linkage);
					scopeputdecl(ps, mkdecl((char *)p->name,
					    DECLTYPE, &typevoid, QUALNONE, LINKNONE));
				}
				next();
			} else if (tok.kind == TAUTO) {
				/* `template<auto N>`: deduced non-type parameter (the C
				 * lexer emits the TAUTO storage-class token for `auto`) */
				next(); /* consume auto */
				p = xmalloc(sizeof(*p));
				p->is_pack = false;
				p->is_dep_nttp = false;
				p->is_nttp = true;
				p->nttp_type = NULL;
				if (tok.kind < TIDENT)
					error_code(E_SYNTAX, &tok.loc, "expected template parameter name");
				p->name = xmalloc(strlen(tokenstr(tok.kind)) + 1);
				strcpy((char *)p->name, tokenstr(tok.kind));
				p->next = NULL;
				*pe = p;
				pe = &p->next;
				++tmpl->nparams;
				next();
			} else if (tok.kind >= TIDENT) {
			/* C++20 constrained type parameter: `template<Concept T>`
			 * (also `template<Concept<Args...> T>` with explicit concept
			 * arguments, and a pack `template<Concept... T>`).  The
			 * constraint `Concept<T>` (or `Concept<Args..., T>`) is
			 * recorded and merged with any requires-clause below;
			 * satisfaction is checked at instantiation by the same
			 * concept machinery as the requires-clause. */
			const char *cnm = tokenstr(tok.kind);
			struct cpp_template *con;
			for (con = g_cpp_templates; con; con = con->next)
				if (con->is_concept && strcmp(con->name, cnm) == 0)
					break;
			if (con) {
				struct token cname = tok;
				bool has_args = false;
				int tdepth = 0;
				next(); /* consume the concept name */
				/* constraint so far: the concept name */
				cpp_constraint_add(&pctoks, &pcn, &pccap, cname);
				/* explicit concept arguments: `template<C<int> T>` */
				if (tok.kind == TLESS) {
					has_args = true;
					for (;;) {
						cpp_constraint_add(&pctoks, &pcn, &pccap, tok);
						if (tok.kind == TLESS)
							++tdepth;
						else if (tok.kind == TGREATER) {
							--tdepth;
							if (tdepth == 0) {
								next();
								break;
							}
						}
						next();
					}
				}
				p = xmalloc(sizeof(*p));
				p->is_pack = false;
				p->is_dep_nttp = false;
				p->is_nttp = false;
				p->nttp_type = NULL;
				if (tok.kind == TELLIPSIS) {
					p->is_pack = true;
					next();
				}
				if (tok.kind < TIDENT)
					error_code(E_SYNTAX, &tok.loc, "expected template parameter name");
				p->name = xmalloc(strlen(tokenstr(tok.kind)) + 1);
				strcpy((char *)p->name, tokenstr(tok.kind));
				p->next = NULL;
				*pe = p;
				pe = &p->next;
				++tmpl->nparams;
				if (!p->is_pack) {
					extern struct decl *mkdecl(char *, enum declkind,
					    struct type *, enum typequal, enum linkage);
					scopeputdecl(ps, mkdecl((char *)p->name,
					    DECLTYPE, &typevoid, QUALNONE, LINKNONE));
				}
				/* complete the constraint: `Concept < [args...,] T >` */
				if (!has_args) {
					struct token t = cname;
					t.kind = TLESS;
					cpp_constraint_add(&pctoks, &pcn, &pccap, t);
				} else {
					/* retract the '>' that closed the explicit args
					 * and insert the parameter as the last argument */
					--pcn;
					struct token t = cname;
					t.kind = TCOMMA;
					cpp_constraint_add(&pctoks, &pcn, &pccap, t);
				}
				{
					struct token t = cname;
					t.kind = tokenget(p->name, strlen(p->name));
					cpp_constraint_add(&pctoks, &pcn, &pccap, t);
					t.kind = TGREATER;
					cpp_constraint_add(&pctoks, &pcn, &pccap, t);
				}
				next(); /* consume the parameter name */
				goto param_done;
			}
			/* not a concept: an identifier here is a *type name* used as
			 * a non-type parameter's type — `template<typename T, T N>`
			 * (dependent NTTP) or a plain fixed-type NTTP.  Fall through
			 * to the common non-type-parameter handling below. */
			goto nttp_common;
			} else {
			/* non-type template parameter with a fixed type: `int N`,
			 * or a type-dependent one: `T N` where T is an earlier
			 * type parameter (nameable in the parameter-list scope). */
			extern struct decl *parameter(struct scope *);
nttp_common:
			struct decl *pd = NULL;
			if (tok.kind >= TIDENT) {
				pd = scopegetdecl(ps, tokenstr(tok.kind), 1);
				if (pd && pd->kind == DECLTYPE) {
					/* `template<typename T, T N>`: dependent NTTP.
					 * nttp_type stays NULL so instantiation binds
					 * the concrete type of the argument. */
					next(); /* consume the type-parameter name */
					if (tok.kind < TIDENT)
						error_code(E_TEMPLATE, &tok.loc,
						    "expected non-type template parameter name");
					p = xmalloc(sizeof(*p));
					p->is_pack = false;
					p->is_dep_nttp = true;
					p->is_nttp = true;
					p->nttp_type = NULL;
					p->name = xmalloc(strlen(tokenstr(tok.kind)) + 1);
					strcpy((char *)p->name, tokenstr(tok.kind));
					p->next = NULL;
					*pe = p;
					pe = &p->next;
					++tmpl->nparams;
					next();
					goto param_done;
				}
				pd = NULL;
			}
			pd = parameter(ps);
			if (!pd || !pd->type ||
			    !(pd->type->prop & (PROPINT | PROPREAL)))
				error_code(E_TEMPLATE, &tok.loc,
				    "non-type template parameter must have integer or enum type");
			p = xmalloc(sizeof(*p));
			p->is_pack = false;
			p->is_dep_nttp = false;
			p->is_nttp = true;
			p->nttp_type = pd->type;
			p->name = pd->name ?
			    strdup(pd->name) : strdup("__nttp");
			p->next = NULL;
			*pe = p;
			pe = &p->next;
			++tmpl->nparams;
		}
param_done:
		/* C++11 default template argument: `template<typename T = int>` /
		 * `template<int N = 5>`.  Buffer the tokens after `=` up to the
		 * next top-level ',' or '>' into the parameter's default span.
		 * Applying the default (for an omitted argument) happens at
		 * instantiation in cpp_tmpl_deduce. */
		p->deftoks = NULL;
		p->ndeftoks = 0;
		if (tok.kind == TASSIGN) {
			struct token *dtoks = NULL;
			size_t dn = 0, dcap = 0, ddep = 0;
			next(); /* consume '=' */
			for (;;) {
				if (tok.kind == TEOF)
					error_code(E_TEMPLATE, &tok.loc,
					    "unterminated default template argument");
				if (ddep == 0 && (tok.kind == TCOMMA || tok.kind == TGREATER))
					break;
				if (tok.kind == TLESS || tok.kind == TLPAREN ||
				    tok.kind == TLBRACK)
					++ddep;
				else if (tok.kind == TGREATER || tok.kind == TRPAREN ||
				    tok.kind == TRBRACK) {
					if (ddep > 0)
						--ddep;
				}
				if (dn >= dcap) {
					dcap = dcap ? dcap * 2 : 16;
					dtoks = xreallocarray(dtoks, dcap, sizeof *dtoks);
				}
				dtoks[dn++] = tok;
				next();
			}
			p->deftoks = dtoks;
			p->ndeftoks = dn;
		}
		if (tok.kind == TGREATER)
			break;
		if (p->is_pack)
			error_code(E_TEMPLATE, &tok.loc, "template parameter pack must be the last parameter");
		expect(TCOMMA, "',' or '>' in template parameter list");
	}
	}
	next(); /* consume '>' */

	/* C++11 template type alias: `template<...> using Name = Type;`.
	 * Registered as a type alias (not a function/class template). */
	if (cpp_tok_kind() == CPP_TUSING) {
		cpp_template_alias(tmpl);
		return;
	}

	/* C++20 concept definition: `template<...> concept Name = expr;`.
	 * The concept body (a constant boolean expression over the template
	 * parameters) is buffered; a use `requires Integral<T>` looks the
	 * concept up and substitutes the argument types. */
	if (cpp_tok_kind() == CPP_TCONCEPT) {
		struct cpp_template *ct = xmalloc(sizeof *ct);
		struct token *ctoks = NULL;
		size_t cn = 0, ccap = 0;
		*ct = *tmpl; /* copy params etc. */
		ct->is_concept = true;
		ct->toks = NULL;
		ct->ntoks = 0;
		ct->constraint = NULL;
		ct->nconstraint = 0;
		ct->insts = NULL;
		ct->insts_end = &ct->insts;
		ct->cls_insts = NULL;
		ct->cls_insts_end = &ct->cls_insts;
		ct->next = NULL;
		free(tmpl); /* the original was just a scaffolding copy */
		tmpl = ct;
		next(); /* consume 'concept' */
		if (tok.kind < TIDENT)
			error_code(E_SYNTAX, &tok.loc, "expected concept name after 'concept'");
		tmpl->name = xmalloc(strlen(tokenstr(tok.kind)) + 1);
		strcpy((char *)tmpl->name, tokenstr(tok.kind));
		next(); /* consume the concept name */
		expect(TASSIGN, "after concept name");
		/* buffer `expr;` — everything up to the ';' at brace depth 0.
		 * A requires-expression body (`requires (T a) { a+a; }`) brings
		 * its own braces and semicolons, so those must not terminate the
		 * concept definition. */
		{
			int cdepth = 0;
			for (;;) {
				if (tok.kind == TEOF)
					error_code(E_CTYPE, &tok.loc,
					    "unterminated concept definition '%s'",
					    tmpl->name);
				if (cdepth == 0 && tok.kind == TSEMICOLON)
					break;
				if (cdepth == 0 &&
				    cpp_tok_kind() == CPP_TREQUIRES) {
					/* a whole requires-expression: consume it
					 * (including its body braces) as one unit */
					struct token *rtoks = NULL;
					size_t rn = cpp_requires_span_len(&rtoks);
					for (size_t k = 0; k < rn; k++) {
						if (cn >= ccap) {
							ccap = ccap ? ccap * 2 : 64;
							ctoks = xreallocarray(ctoks,
							    ccap, sizeof *ctoks);
						}
						ctoks[cn++] = rtoks[k];
					}
					free(rtoks);
					continue;
				}
				if (tok.kind == TLBRACE)
					++cdepth;
				else if (tok.kind == TRBRACE && cdepth > 0)
					--cdepth;
				if (cn >= ccap) {
					ccap = ccap ? ccap * 2 : 64;
					ctoks = xreallocarray(ctoks, ccap,
					    sizeof *ctoks);
				}
				ctoks[cn++] = tok;
				next();
			}
		}
		next(); /* consume ';' */
		tmpl->toks = ctoks;
		tmpl->ntoks = cn;
		*g_cpp_templates_end = tmpl;
		g_cpp_templates_end = &tmpl->next;
		return;
	}

	/* C++20 requires-clause: `template<...> requires Expr<T> decl`.
	 * Buffer the constraint expression tokens (everything from `requires`
	 * up to the start of the declaration).  The constraint is a boolean
	 * expression over concept uses (`Small<T>`, `Small<T> && NotVoid<T>`,
	 * `!Small<T>`); we consume tokens until the return type / function
	 * name of the declaration begins, or until '{' / ';'.  A constraint
	 * combinator (`&&` / `||` / `!`) keeps the following concept name in
	 * the clause. */
	if (cpp_tok_kind() == CPP_TREQUIRES) {
		struct token *ctoks = NULL;
		size_t cn = 0, ccap = 0;
		int depth = 0;
		bool after_op = false; /* previous token was && / || / ! */
		next(); /* consume 'requires' */
		for (;;) {
			if (tok.kind == TEOF)
				break;
			if (depth == 0 && (tok.kind == TLBRACE || tok.kind == TSEMICOLON))
				break;
			/* The declaration begins with the return type (a keyword
			 * like `int` or a type/function name), or '{' / ';'.  A
			 * constraint combinator (`&&` / `||` / `!`) means the
			 * next identifier is another concept name and stays part
			 * of the requires-clause; identifiers and template-arg
			 * brackets are also constraint tokens. */
			if (depth == 0 && cn > 0 && !after_op &&
			    (tok.kind >= TIDENT ||
			     (tok.kind != TLESS && tok.kind != TGREATER &&
			      tok.kind != TCOMMA && tok.kind != TLAND &&
			      tok.kind != TLOR && tok.kind != TLNOT)))
				break;
			if (tok.kind == TLESS || tok.kind == TLPAREN || tok.kind == TLBRACK)
				++depth;
			else if (tok.kind == TGREATER || tok.kind == TRPAREN ||
			    tok.kind == TRBRACK) {
				if (depth > 0)
					--depth;
			}
			if (cn >= ccap) {
				ccap = ccap ? ccap * 2 : 16;
				ctoks = xreallocarray(ctoks, ccap, sizeof *ctoks);
			}
			ctoks[cn++] = tok;
			after_op = tok.kind == TLAND || tok.kind == TLOR ||
			    tok.kind == TLNOT;
			next();
		}
		tmpl->constraint = ctoks;
		tmpl->nconstraint = cn;
		if (pcn) {
			/* merge the `template<Concept T>` parameter constraints
			 * with the requires-clause: `Concept<T> && Expr<T>` */
			struct token t = tok;
			struct token *all = xmalloc((pcn + 1 + cn) * sizeof *all);
			size_t an = 0, k;
			for (k = 0; k < pcn; k++)
				all[an++] = pctoks[k];
			t.kind = TLAND;
			all[an++] = t;
			for (k = 0; k < cn; k++)
				all[an++] = ctoks[k];
			tmpl->constraint = all;
			tmpl->nconstraint = an;
			free(ctoks);
			free(pctoks);
		}
	} else if (pcn) {
		/* no requires-clause: the parameter constraints are the whole
		 * constraint */
		tmpl->constraint = pctoks;
		tmpl->nconstraint = pcn;
	}

	/* class template: `template<...> class Foo { ... }` (struct/union too).
	 * Checked *after* the requires-clause: with `template<...> requires C<T>
	 * class Foo { ... };` the token following '>' is `requires`, not the
	 * class-key, so probing before the clause is consumed would leave
	 * is_class false and the trailing ';' of `};` unconsumed. */
	{
		enum cpp_tokenkind k = cpp_tok_kind();
		if (k == CPP_TCLASS || k == CPP_TSTRUCT || k == CPP_TUNION)
			tmpl->is_class = true;
	}

	/* buffer the rest of the declaration (return type .. body / ';') */
	toks = NULL;
	for (;;) {
		/* declaration-only template: stop at the ';' */
		if (bd == 0 && tok.kind == TSEMICOLON) {
			next();
			break;
		}
		if (ntoks >= cap) {
			cap = cap ? cap * 2 : 128;
			toks = xreallocarray(toks, cap, sizeof *toks);
		}
		toks[ntoks++] = tok;
		if (tok.kind == TLBRACE)
			++bd;
		else if (tok.kind == TRBRACE)
			--bd;
		next();
		/* end of a function body: the closing brace brought bd back to 0 */
		if (bd == 0 && toks[ntoks - 1].kind == TRBRACE)
			break;
		if (tok.kind == TEOF)
			break;
	}
	/* a class template's `};` leaves the trailing ';' here; consume it */
	if (tmpl->is_class && tok.kind == TSEMICOLON)
		next();
	tmpl->toks = toks;
	tmpl->ntoks = ntoks;

	/* the function name: first plain identifier (not a C++ keyword, not a
	 * template parameter name) in the buffered declaration.  Identifier
	 * tokens carry their spelling in the interned token table (tokenstr
	 * of the kind); `lit` points at the scanner's reused buffer and is
	 * not stable for identifiers. */
	for (size_t i = 0; i < ntoks; ++i) {
		const char *nm;
		if (toks[i].kind < TIDENT)
			continue;
		nm = tokenstr(toks[i].kind);
		/* a template operator overload: `template<typename T> auto
		 * operator()(...)`.  The method name is `operator_<op>` ("cl"
		 * for operator(), "pl" for operator+), matching the non-template
		 * lowering so the call site resolves it. */
		if (cpp_classify_ident(nm, strlen(nm)) == CPP_TOPERATOR) {
			const char *onm = NULL;
			if (i + 1 < ntoks)
				onm = cpp_op_mangle(toks[i + 1].kind);
			if (onm) {
				char *m = xmalloc(strlen("operator_") + strlen(onm) + 1);
				sprintf(m, "operator_%s", onm);
				tmpl->name = m;
				break;
			}
			continue;
		}
		if (cpp_classify_ident(nm, strlen(nm)) != CPP_TNONE)
			continue; /* keyword */
		param = false;
		for (p = tmpl->params; p; p = p->next)
			if (strcmp(p->name, nm) == 0) {
				param = true;
				break;
			}
		if (!param) {
			tmpl->name = xmalloc(strlen(nm) + 1);
			strcpy((char *)tmpl->name, nm);
			break;
		}
	}
	if (!tmpl->name)
		error_code(E_DECL, &tok.loc, "unable to determine template function name");

	*g_cpp_templates_end = tmpl;
	g_cpp_templates_end = &tmpl->next;
}

/* Is template parameter `i` a non-type parameter? */
bool
tmpl_param_is_nttp(struct cpp_template *tmpl, int i)
{
	struct cpp_tmpl_param *p;
	for (p = tmpl->params; p && i > 0; p = p->next, --i)
		;
	return p && p->is_nttp;
}
