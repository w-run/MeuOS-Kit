/* mcc/lex - internal declarations shared by pp.c and pp_expr.c.
 * The #if constant-expression arithmetic evaluator lives in pp_expr.c;
 * pp.c calls evalconst() from evalexpr() after collecting/expanding the
 * controlling expression tokens. */
#ifndef MCC_LEX_PP_INTERNAL_H
#define MCC_LEX_PP_INTERNAL_H

#include <stddef.h>

struct token;

/* Evaluate a sequence of preprocessor tokens as a #if integral constant
 * expression. Defined in pp_expr.c. */
long long evalconst(struct token *t, size_t n);

#endif /* MCC_LEX_PP_INTERNAL_H */
