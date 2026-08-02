#ifndef MCC_PARSE_EXPR_INTERNAL_H
#define MCC_PARSE_EXPR_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Internal forward declarations shared by the split src/parse/expr_*.c
 * files. The expression grammar is split by precedence tier:
 *
 *   expr.c           constructors + top-level expr() / assignexpr()
 *   expr_literal.c   inttype/octval/hexval/decodechar/encodechar-N/stringconcat
 *   expr_primary.c   primaryexpr / designator / builtinfunc
 *   expr_postfix.c   mkincdecexpr / postfixexpr
 *   expr_unary.c     unaryexpr / castexpr
 *   expr_binary.c    precedence / binaryexpr / condexpr
 *   expr_generic.c   generic / intconstexpr
 *
 * Each tier calls into the tier below (and into expr.c's constructors),
 * so every file needs most of the function signatures below. The decls
 * here are non-static so the linker can resolve cross-file calls. */

/* Constructors / operators (defined in expr.c). */
struct expr *mkexpr(enum exprkind, struct type *, struct expr *);
void delexpr(struct expr *);
struct expr *mkconstexpr(struct type *, unsigned long long);
struct expr *decay(struct expr *);
struct expr *mkunaryexpr(enum tokenkind, struct expr *);
unsigned bitfieldwidth(struct expr *);
struct expr *exprconvert(struct expr *, struct type *);
bool nullpointer(struct expr *);
struct expr *exprassign(struct expr *, struct type *);
struct expr *exprpromote(struct expr *);
struct type *commonreal(struct expr **, struct expr **);
struct expr *mksizeofexpr(struct type *);
struct expr *mkbinaryexpr(struct location *, enum tokenkind, struct expr *, struct expr *);
struct expr *assignexpr(struct scope *);
struct expr *mkassignexpr(struct expr *, struct expr *);

/* Literal decoders / encoders (defined in expr_literal.c). */
struct type *inttype(unsigned long long, bool, char *);
size_t decodechar(const char *, uint_least32_t *, bool *, const char *, struct location *);
struct type *stringconcat(struct stringlit *, bool);

/* Grammar tiers. */
struct expr *generic(struct scope *);
unsigned long long intconstexpr(struct scope *, bool);
struct expr *primaryexpr(struct scope *);
struct expr *builtinfunc(struct scope *, enum builtinkind);
struct expr *mkincdecexpr(enum tokenkind, struct expr *, bool);
struct expr *postfixexpr(struct scope *, struct expr *);
struct expr *unaryexpr(struct scope *);
struct expr *castexpr(struct scope *);
struct expr *binaryexpr(struct scope *, struct expr *, int);
struct expr *condexpr(struct scope *);

#endif
