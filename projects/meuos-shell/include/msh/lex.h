/* msh/lex.h — 词法分析器接口
 *
 * 骨架阶段：本文件为空 stub，仅占位。完整实现在 P6 阶段。
 *
 * 将来接口：
 *   enum msh_tok {
 *       TOK_WORD, TOK_EOF, TOK_NEWLINE,
 *       TOK_PIPE, TOK_AND, TOK_OR,
 *       TOK_REDIR_IN, TOK_REDIR_OUT, TOK_REDIR_APPEND, TOK_REDIR_DUP,
 *       TOK_SEMI, TOK_AMP,
 *       TOK_LPAREN, TOK_RPAREN,
 *       TOK_DOLLAR, TOK_DOLLAR_LBRACE,
 *       TOK_FOR, TOK_WHILE, TOK_IF, TOK_THEN, TOK_ELSE, TOK_FI,
 *       ...
 *   };
 *
 *   struct msh_lexer {
 *       const char *input;
 *       size_t pos;
 *       ...
 *   };
 */
#ifndef MEUOS_MSH_LEX_H
#define MEUOS_MSH_LEX_H

/* 骨架阶段：保留头文件为空 */
#include "msh/msh.h"

#endif /* MEUOS_MSH_LEX_H */
