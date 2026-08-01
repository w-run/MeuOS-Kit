/* msh 词法分析器 - 实现见 lex.h 中的 lexer_t 结构定义 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "msh/lex.h"
#include "msh/msh.h"

void msh_lexer_init(lexer_t *lx, const char *src, size_t len) {
    lx->src = src;
    lx->pos = 0;
    lx->len = len;
    lx->lineno = 1;
    lx->word_buf = NULL;
    lx->word_cap = 0;
    lx->word_len = 0;
    lx->in_word = 0;
    lx->pending_op = 0;
    lx->at_cmd_start = 1;  /* 文件起始视为命令起始 */
    lx->was_quoted = 0;
}

void msh_lexer_free(lexer_t *lx) {
    if (lx) free(lx->word_buf);
    lx->word_buf = NULL;
}

static void word_reset(lexer_t *lx) {
    lx->word_len = 0;
    lx->in_word = 0;
}

static void word_append(lexer_t *lx, char c) {
    if (lx->word_len + 1 >= lx->word_cap) {
        if (lx->word_cap == 0) lx->word_cap = 32;
        else lx->word_cap *= 2;
        lx->word_buf = realloc(lx->word_buf, lx->word_cap);
    }
    lx->word_buf[lx->word_len++] = c;
    lx->word_buf[lx->word_len] = '\0';
}

static int peek(lexer_t *lx) {
    if (lx->pos >= lx->len) return -1;
    return (unsigned char)lx->src[lx->pos];
}

static int next(lexer_t *lx) {
    if (lx->pos >= lx->len) return -1;
    int c = (unsigned char)lx->src[lx->pos++];
    if (c == '\n') lx->lineno++;
    return c;
}

static int is_op_char(int c) {
    return c == '|' || c == '&' || c == ';' || c == '<' || c == '>'
           || c == '(' || c == ')';
}

static char *take_word(lexer_t *lx) {
    /* word_len==0 且 in_word==0 表示没有词；in_word==1 表示空引号（""）→ 返回空串 */
    if (lx->word_len == 0 && !lx->in_word) return NULL;
    char *r = malloc(lx->word_len + 1);
    memcpy(r, lx->word_buf, lx->word_len + 1);
    word_reset(lx);
    return r;
}

/* 在命令起始位置时，将 word 文本映射为保留字 token；否则返回 TOK_WORD */
static int classify_word(const char *s) {
    if (!s) return TOK_WORD;
    /* 保留字表 */
    if (!strcmp(s, "if"))       return TOK_IF;
    if (!strcmp(s, "then"))     return TOK_THEN;
    if (!strcmp(s, "elif"))     return TOK_ELIF;
    if (!strcmp(s, "else"))     return TOK_ELSE;
    if (!strcmp(s, "fi"))       return TOK_FI;
    if (!strcmp(s, "for"))      return TOK_FOR;
    if (!strcmp(s, "in"))       return TOK_IN;
    if (!strcmp(s, "do"))       return TOK_DO;
    if (!strcmp(s, "done"))     return TOK_DONE;
    if (!strcmp(s, "while"))    return TOK_WHILE;
    if (!strcmp(s, "until"))    return TOK_UNTIL;
    if (!strcmp(s, "case"))     return TOK_CASE;
    if (!strcmp(s, "esac"))     return TOK_ESAC;
    if (!strcmp(s, "function")) return TOK_FUNCTION;
    if (!strcmp(s, "{"))       return TOK_LBRACE;
    if (!strcmp(s, "}"))       return TOK_RBRACE;
    if (!strcmp(s, "!"))       return TOK_BANG;
    if (!strcmp(s, "[["))      return TOK_DLBRACK;
    if (!strcmp(s, "]]"))      return TOK_DRBRACK;
    return TOK_WORD;
}

/* word token 发出后更新 at_cmd_start：
 * `{` `then` `do` `else` `elif` 后是新命令起始；
 * `}` `fi` `done` `esac` 后不是。
 * 注意：`{` 在非命令起始位置也是 WORD（如 f() 之后），但仍要设置 cmd_start。 */
static void word_done(lexer_t *lx, int tok, const char *text) {
    if (tok == TOK_LBRACE || (text && strcmp(text, "{") == 0)
        || tok == TOK_THEN || (text && strcmp(text, "then") == 0)
        || tok == TOK_DO || (text && strcmp(text, "do") == 0)
        || tok == TOK_ELSE || (text && strcmp(text, "else") == 0)
        || tok == TOK_ELIF || (text && strcmp(text, "elif") == 0)
        || tok == TOK_IF || (text && strcmp(text, "if") == 0)
        || tok == TOK_WHILE || (text && strcmp(text, "while") == 0)
        || tok == TOK_UNTIL || (text && strcmp(text, "until") == 0)
        || tok == TOK_FOR || (text && strcmp(text, "for") == 0)
        || tok == TOK_LPAREN
        || tok == TOK_AND || tok == TOK_OR
        || tok == TOK_SEMI || tok == TOK_NEWLINE
        || tok == TOK_PIPE) {
        lx->at_cmd_start = 1;
    } else {
        lx->at_cmd_start = 0;
    }
}

/* 扫描配对的 `)`/`}`，引用与括号嵌套感知。
 * 起始位置在 `start_pos`（已消费了 `$(` 或 `$((` 或 `${` 中的开括号）。
 * `open` 是开括号 '(' 或 '{'，返回消费到闭合符后下一个位置。
 * 该函数仅负责把整段子串原样追加到 word_buf；它**不**展开，展开在 exec 期。*/
static void scan_balanced(lexer_t *lx, char open, char close) {
    /* 调用方已经消费了 open。word_buf 之前已经追加了 "$(" / "$((" / "${"。
     * 这里扫描配对的 close，处理嵌套与引号。 */
    int depth = 1;
    int c;
    while (depth > 0 && (c = next(lx)) != -1) {
        word_append(lx, (char)c);
        if (c == '\'') {
            /* 单引号：原样追加直到下一个 ' */
            while ((c = next(lx)) != -1) {
                word_append(lx, (char)c);
                if (c == '\'') break;
            }
            continue;
        }
        if (c == '"') {
            while ((c = next(lx)) != -1) {
                word_append(lx, (char)c);
                if (c == '\\') {
                    int n = next(lx);
                    if (n != -1) word_append(lx, (char)n);
                    continue;
                }
                if (c == '"') break;
            }
            continue;
        }
        if (c == open) depth++;
        else if (c == close) depth--;
    }
    /* 闭合符已追加进 word_buf */
}

/* here-doc 扫描：从当前位置扫描到delimiter行，返回收集的内容（malloc）。
 * 成功返回内容（可能为空串），lx->pos 推进到 delimiter 行尾下一个位置。
 * 失败（未找到 delimiter）返回 NULL。
 * 注意：本函数不处理 delimiter 的引号剥离或内容展开，由调用者决定。 */
char *msh_lex_heredoc(lexer_t *lx, const char *delimiter) {
    if (!delimiter || !*delimiter) return NULL;
    size_t dlen = strlen(delimiter);
    size_t cap = 256, len = 0;
    char *buf = malloc(cap);
    if (!buf) return NULL;
    buf[0] = '\0';

    while (lx->pos < lx->len) {
        /* 行首 */
        size_t line_start = lx->pos;
        /* 扫描到行尾（\n 或 EOF） */
        size_t j = lx->pos;
        while (j < lx->len && lx->src[j] != '\n') j++;
        size_t line_len = j - lx->pos;  /* 不含 \n */

        /* 检查是否匹配 delimiter（整行恰好是 delimiter） */
        if (line_len == dlen && memcmp(lx->src + lx->pos, delimiter, dlen) == 0) {
            /* 跳过 delimiter 行 + 换行 */
            lx->pos = j;
            if (lx->pos < lx->len && lx->src[lx->pos] == '\n') {
                lx->pos++;
                lx->lineno++;
            }
            return buf;
        }

        /* 不是 delimiter：追加本行 + 换行 */
        while (len + line_len + 2 >= cap) {
            cap *= 2;
            buf = realloc(buf, cap);
        }
        memcpy(buf + len, lx->src + line_start, line_len);
        len += line_len;
        buf[len++] = '\n';
        buf[len] = '\0';
        lx->pos = j;
        if (lx->pos < lx->len && lx->src[lx->pos] == '\n') {
            lx->pos++;
            lx->lineno++;
        }
    }
    /* 未找到 delimiter */
    free(buf);
    fprintf(stderr, "msh: warning: heredoc delimited by EOF (looking for %s)\n", delimiter);
    return NULL;
}

/* 主词法入口 */
int msh_lex_next(lexer_t *lx, char **out_text) {
    if (out_text) *out_text = NULL;
    int c;

    /* 若有上次未返回的运算符，先返回 */
    if (lx->pending_op) {
        int op = lx->pending_op;
        lx->pending_op = 0;
        /* 这些 token 后是新的命令起始 */
        if (op == TOK_NEWLINE || op == TOK_SEMI || op == TOK_PIPE
            || op == TOK_AND || op == TOK_OR || op == TOK_LBRACE
            || op == TOK_LPAREN) {
            lx->at_cmd_start = 1;
        } else {
            lx->at_cmd_start = 0;
        }
        return op;
    }

    while ((c = next(lx)) != -1) {
        if (c == ' ' || c == '\t') {
            if (lx->in_word) {
                char *w = take_word(lx);
                int tok = TOK_WORD;
                if (lx->at_cmd_start) tok = classify_word(w);
                if (out_text) *out_text = w;
                else free(w);
                word_done(lx, tok, w);
                return tok;
            }
            continue;
        }
        if (c == '\n') {
            if (lx->in_word) {
                char *w = take_word(lx);
                int tok = TOK_WORD;
                if (lx->at_cmd_start) tok = classify_word(w);
                if (out_text) *out_text = w;
                else free(w);
                word_done(lx, tok, w);
                /* 紧接着 NEWLINE 也是命令起始：但我们已返回 WORD，pending 推 NEWLINE */
                lx->pending_op = TOK_NEWLINE;
                return tok;
            }
            lx->at_cmd_start = 1;
            return TOK_NEWLINE;
        }
        if (c == '#' && !lx->in_word) {
            /* 注释仅在 word 起始位置（前面是空白/运算符）。
             * 在 word 中间（如 a#b 或 $#）的 # 不是注释。 */
            while (c != '\n' && c != -1) c = next(lx);
            if (c == '\n') {
                lx->at_cmd_start = 1;
                return TOK_NEWLINE;
            }
            return TOK_EOF;
        }
        if (c == '\\') {
            int n = next(lx);
            if (n == '\n') continue;
            if (n == -1) break;
            word_append(lx, (char)n);
            lx->in_word = 1;
            continue;
        }
        if (c == '\'') {
            while ((c = next(lx)) != -1 && c != '\'') {
                word_append(lx, (char)c);
            }
            if (c != '\'') {
                fprintf(stderr, "msh: unterminated single quote near line %d\n", lx->lineno);
                return TOK_EOF;
            }
            lx->in_word = 1;
            lx->was_quoted = 1;  /* 单引号 */
            continue;
        }
        if (c == '"') {
            while ((c = next(lx)) != -1 && c != '"') {
                if (c == '\\') {
                    int n = next(lx);
                    if (n != -1) word_append(lx, (char)n);
                } else {
                    word_append(lx, (char)c);
                }
                lx->in_word = 1;
            }
            if (c != '"') {
                fprintf(stderr, "msh: unterminated double quote near line %d\n", lx->lineno);
                return TOK_EOF;
            }
            lx->in_word = 1;
            lx->was_quoted = 2;  /* 双引号 */
            continue;
        }
        if (c == '$') {
            int next_c = peek(lx);
            /* $(...) 或 $((...)) ：原样保留整段子串（含 $( 和 )) */
            if (next_c == '(') {
                word_append(lx, '$');
                word_append(lx, '(');
                next(lx);  /* 消费 ( */
                if (peek(lx) == '(') {
                    word_append(lx, '(');
                    next(lx);
                    scan_balanced(lx, '(', ')');
                    /* 此时第二个 ) 已被 scan_balanced 消费并追加；
                     * 但 $((...)) 实际还需要再一个 )。继续扫描配对 */
                    scan_balanced(lx, '(', ')');
                } else {
                    scan_balanced(lx, '(', ')');
                }
                lx->in_word = 1;
                continue;
            }
            /* ${...}：原样保留 */
            if (next_c == '{') {
                word_append(lx, '$');
                word_append(lx, '{');
                next(lx);  /* 消费 { */
                scan_balanced(lx, '{', '}');
                lx->in_word = 1;
                continue;
            }
            /* 普通 $VAR / $? / $1 等：原样保留 $ 与后续字符，让 expand 期处理 */
            word_append(lx, '$');
            lx->in_word = 1;
            continue;
        }
        if (c == '`') {
            /* 反引号命令替换：原样保留整段（含反引号），expand 期处理 */
            word_append(lx, '`');
            int n;
            while ((n = next(lx)) != -1) {
                word_append(lx, (char)n);
                if (n == '`') break;
                if (n == '\\') {
                    int e = next(lx);
                    if (e != -1) word_append(lx, (char)e);
                }
            }
            lx->in_word = 1;
            continue;
        }
        if (is_op_char(c)) {
            /* bash array: if word ends with = and c is '(', include balanced () */
            if (c == '(' && lx->in_word && lx->word_len > 0 &&
                lx->word_buf[lx->word_len - 1] == '=') {
                word_append(lx, '(');
                int paren_depth = 1;
                int nc;
                while ((nc = next(lx)) != -1) {
                    word_append(lx, (char)nc);
                    if (nc == '(') paren_depth++;
                    else if (nc == ')') {
                        paren_depth--;
                        if (paren_depth == 0) break;
                    }
                }
                lx->in_word = 1;
                continue;
            }
            if (lx->in_word) {
                char *w = take_word(lx);
                int tok = TOK_WORD;
                if (lx->at_cmd_start) tok = classify_word(w);
                if (w && out_text) *out_text = w;
                else free(w);
                int n2 = peek(lx);
                switch (c) {
                case '|':
                    lx->pending_op = (n2 == '|') ? TOK_OR : TOK_PIPE;
                    if (n2 == '|') next(lx);
                    break;
                case '&':
                    lx->pending_op = (n2 == '&') ? TOK_AND : TOK_BG;
                    if (n2 == '&') next(lx);
                    break;
                case ';': lx->pending_op = TOK_SEMI; break;
                case '<':
                    if (n2 == '<') {
                        next(lx);  /* consume second < */
                        if (peek(lx) == '<') {
                            next(lx);  /* consume third < */
                            lx->pending_op = TOK_HERESTRING;
                        } else {
                            lx->pending_op = TOK_HEREDOC;
                        }
                    } else if (n2 == '&') { next(lx); lx->pending_op = TOK_REDIR_DUP_IN; }
                    else lx->pending_op = TOK_REDIR_IN;
                    break;
                case '>':
                    if (n2 == '>') { next(lx); lx->pending_op = TOK_REDIR_APPEND; }
                    else if (n2 == '&') { next(lx); lx->pending_op = TOK_REDIR_DUP_OUT; }
                    else lx->pending_op = TOK_REDIR_OUT;
                    break;
                case '(': lx->pending_op = TOK_LPAREN; break;
                case ')': lx->pending_op = TOK_RPAREN; break;
                default: lx->pending_op = c; break;
                }
                word_done(lx, tok, w);
                return tok;
            }
            int n2 = peek(lx);
            switch (c) {
            case '|':
                if (n2 == '|') { next(lx); lx->at_cmd_start = 1; return TOK_OR; }
                lx->at_cmd_start = 1;
                return TOK_PIPE;
            case '&':
                if (n2 == '&') { next(lx); lx->at_cmd_start = 1; return TOK_AND; }
                lx->at_cmd_start = 1;
                return TOK_BG;
            case ';':
                lx->at_cmd_start = 1;
                return TOK_SEMI;
            case '<':
                if (n2 == '<') {
                    next(lx);  /* consume second < */
                    if (peek(lx) == '<') {
                        next(lx);  /* consume third < */
                        lx->at_cmd_start = 0;
                        return TOK_HERESTRING;
                    }
                    lx->at_cmd_start = 0;
                    return TOK_HEREDOC;
                }
                if (n2 == '&') { next(lx); lx->at_cmd_start = 0; return TOK_REDIR_DUP_IN; }
                lx->at_cmd_start = 0;
                return TOK_REDIR_IN;
            case '>':
                if (n2 == '>') { next(lx); lx->at_cmd_start = 0; return TOK_REDIR_APPEND; }
                if (n2 == '&') { next(lx); lx->at_cmd_start = 0; return TOK_REDIR_DUP_OUT; }
                lx->at_cmd_start = 0;
                return TOK_REDIR_OUT;
            case '(':
                lx->at_cmd_start = 1;
                return TOK_LPAREN;
            case ')':
                lx->at_cmd_start = 0;
                return TOK_RPAREN;
            }
            lx->at_cmd_start = 0;
            return c;
        }
        word_append(lx, (char)c);
        lx->in_word = 1;
        lx->was_quoted = 0;  /* 普通字符，无引号 */
    }

    char *w = take_word(lx);
    if (w) {
        int tok = TOK_WORD;
        if (lx->at_cmd_start) tok = classify_word(w);
        if (out_text) *out_text = w;
        else free(w);
        word_done(lx, tok, w);
        return tok;
    }
    return TOK_EOF;
}
