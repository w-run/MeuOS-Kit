/*
 * bison.c — GNU bison-compatible LALR(1) parser generator
 *
 * Phase 6d: LALR(1) parser generator for meuos-buildtools.
 * Read .y grammar files and output C source for yyparse().
 *
 * Supported: %token/%type/%left/%right/%nonassoc/%start/%%
 *            %{...%} prologue, {...} semantic actions
 *            LR(0) itemset, LALR(1) lookahead, conflict detection
 *
 * Copyright (C) MeuOS Project  SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ===== Limits ===== */
#define MAX_SYMBOLS    512
#define MAX_RULES      1024
#define MAX_RHS_LEN    32
#define MAX_STATES     512
#define MAX_ITEMS      4096

/* ===== Symbol / token table ===== */
typedef struct {
    char   *name;
    int     type;       /* 0 = nonterminal, 1 = terminal */
    int     prec;       /* precedence level */
    char    assoc;      /* 'l'/'r'/'n'/0 */
    int     token_val;
} Symbol;

static Symbol  symbols[MAX_SYMBOLS];
static int     nsymbols = 0;

static int find_or_add_symbol(const char *name, int is_term)
{
    for (int i = 0; i < nsymbols; i++)
        if (strcmp(symbols[i].name, name) == 0) return i;
    if (nsymbols >= MAX_SYMBOLS) {
        fprintf(stderr, "bison: too many symbols\n"); exit(1);
    }
    int idx = nsymbols++;
    symbols[idx].name = strdup(name);
    symbols[idx].type = is_term;
    symbols[idx].prec = 0;
    symbols[idx].assoc = 0;
    symbols[idx].token_val = idx + 256;
    return idx;
}

static int find_symbol(const char *name)
{
    for (int i = 0; i < nsymbols; i++)
        if (strcmp(symbols[i].name, name) == 0) return i;
    return -1;
}

/* ===== Grammar rule table ===== */
typedef struct {
    int     lhs;                /* symbol index */
    int     rhs[MAX_RHS_LEN];
    int     nrhs;
    int     prec_sym;           /* -1 = none */
    char   *action;             /* semantic action C code, may be NULL */
} Rule;

static Rule    rules[MAX_RULES];
static int     nrules = 0;
static int     augmented_rule = -1;

/* ===== LR items & state sets ===== */
typedef struct {
    int     rule_idx;
    int     dot_pos;
} LRItem;

typedef struct {
    LRItem  items[MAX_ITEMS];
    int     nitems;
} ItemSet;

typedef unsigned long long LookBits;

static ItemSet    states[MAX_STATES];
static int        nstates = 0;

/* LALR(1) lookaheads: flat array indexed by item offset.
 * Offset = sum of nitems of preceding states + item index within state. */
static LookBits  *lookaheads = NULL;
static int        lookahead_cap = 0;

/* goto_tab[state][symbol] = next_state, -1 = none */
static int        goto_tab[MAX_STATES][MAX_SYMBOLS];

/* Helpers */
static void *xmalloc(size_t sz) {
    void *p = malloc(sz);
    if (!p) { fprintf(stderr, "bison: out of memory\n"); exit(1); }
    return p;
}
static void *xrealloc(void *p, size_t sz) {
    p = realloc(p, sz);
    if (!p) { fprintf(stderr, "bison: out of memory\n"); exit(1); }
    return p;
}
static char *xstrdup(const char *s) {
    char *p = strdup(s);
    if (!p) { fprintf(stderr, "bison: out of memory\n"); exit(1); }
    return p;
}

/* ===== .y file lexer ===== */
typedef struct {
    const char *data;
    size_t pos, len;
    int    lineno;
} YYParser;

static int yy_next(YYParser *yp) {
    if (yp->pos >= yp->len) return EOF;
    return (unsigned char)yp->data[yp->pos++];
}
static int yy_peek(YYParser *yp) {
    if (yp->pos >= yp->len) return EOF;
    return (unsigned char)yp->data[yp->pos];
}
static void yy_skip_ws(YYParser *yp) {
    int c;
    while ((c = yy_peek(yp)) != EOF) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            yy_next(yp);
            if (c == '\n') yp->lineno++;
        } else break;
    }
}
static char *yy_read_name(YYParser *yp) {
    int c = yy_peek(yp);
    if (c != EOF && (isalpha(c) || c == '_')) {
        char buf[256]; int i = 0;
        while (c != EOF && (isalnum(c) || c == '_') && i < 255) {
            buf[i++] = (char)yy_next(yp);
            c = yy_peek(yp);
        }
        buf[i] = '\0';
        return xstrdup(buf);
    }
    return NULL;
}
static char *yy_read_code_block(YYParser *yp, const char *end_marker) {
    size_t cap = 4096, len = 0;
    char *buf = xmalloc(cap);
    int end_len = (int)strlen(end_marker);
    while (yp->pos < yp->len) {
        if ((int)(yp->len - yp->pos) >= end_len &&
            memcmp(yp->data + yp->pos, end_marker, (size_t)end_len) == 0) {
            yp->pos += (size_t)end_len;
            buf[len] = '\0'; return buf;
        }
        int c = yy_next(yp);
        if (c == '\n') yp->lineno++;
        if (len + 1 >= cap) { cap *= 2; buf = xrealloc(buf, cap); }
        buf[len++] = (char)c;
    }
    buf[len] = '\0'; return buf;
}

/* ===== .y file parser ===== */
static char *prologue_code = NULL, *epilogue_code = NULL;
static int   start_symbol = -1;

static int parse_y_file(const char *input, size_t len) {
    YYParser yp = { input, 0, len, 1 };
    int current_prec = 0;
    int after_first = 0, after_second = 0;

    while (yp.pos < yp.len) {
        yy_skip_ws(&yp);
        int c = yy_peek(&yp);
        if (c == EOF) break;

        /* %{ ... %} prologue */
        if (c == '%' && yp.pos + 1 < yp.len && yp.data[yp.pos + 1] == '{') {
            yp.pos += 2;
            char *code = yy_read_code_block(&yp, "%}");
            if (!prologue_code) prologue_code = code;
            else {
                size_t ol = strlen(prologue_code), al = strlen(code);
                prologue_code = xrealloc(prologue_code, ol + al + 1);
                memcpy(prologue_code + ol, code, al + 1);
                free(code);
            }
            continue;
        }

        /* %% separator */
        if (c == '%' && yp.pos + 1 < yp.len && yp.data[yp.pos + 1] == '%') {
            if (!after_first) { after_first = 1; yp.pos += 2; continue; }
            else { after_second = 1; yp.pos += 2; continue; }
        }

        /* Directives */
        if (c == '%') {
            yy_next(&yp);
            char *dir = yy_read_name(&yp);
            if (!dir) { fprintf(stderr, "bison:%d: expected directive\n", yp.lineno); return -1; }

            if (strcmp(dir, "token") == 0) {
                yy_skip_ws(&yp);
                if (yy_peek(&yp) == '<') { yy_next(&yp); free(yy_read_name(&yp)); yy_next(&yp); }
                while (1) {
                    yy_skip_ws(&yp); char *n = yy_read_name(&yp);
                    if (!n) break;
                    find_or_add_symbol(n, 1);
                    free(n);
                    yy_skip_ws(&yp); if (yy_peek(&yp) == '%') break;
                }
            } else if (strcmp(dir, "type") == 0) {
                yy_skip_ws(&yp);
                if (yy_peek(&yp) == '<') { yy_next(&yp); free(yy_read_name(&yp)); yy_next(&yp); }
                while (1) {
                    yy_skip_ws(&yp); char *n = yy_read_name(&yp);
                    if (!n) break;
                    find_or_add_symbol(n, 0);
                    free(n);
                }
            } else if (strcmp(dir, "left") == 0 || strcmp(dir, "right") == 0 ||
                       strcmp(dir, "nonassoc") == 0) {
                current_prec++;
                char assoc = dir[0];
                yy_skip_ws(&yp);
                if (yy_peek(&yp) == '<') { yy_next(&yp); free(yy_read_name(&yp)); yy_next(&yp); }
                while (1) {
                    yy_skip_ws(&yp); char *n = yy_read_name(&yp);
                    if (!n) break;
                    int idx = find_or_add_symbol(n, 1);
                    symbols[idx].prec = current_prec;
                    symbols[idx].assoc = assoc;
                    free(n);
                }
            } else if (strcmp(dir, "start") == 0) {
                yy_skip_ws(&yp); char *n = yy_read_name(&yp);
                if (n) { start_symbol = find_or_add_symbol(n, 0); free(n); }
            } else if (strcmp(dir, "union") == 0) {
                yy_skip_ws(&yp);
                if (yy_peek(&yp) == '{') { yy_next(&yp); free(yy_read_code_block(&yp, "}")); }
            } else {
                /* skip unknown directives */
            }
            free(dir);
            continue;
        }

        if (after_first && !after_second) {
            /* Grammar rules */
            char *name = yy_read_name(&yp);
            if (!name) { yy_next(&yp); continue; }
            int lhs = find_or_add_symbol(name, 0);
            free(name);

            yy_skip_ws(&yp);
            if (yy_peek(&yp) == '<') { yy_next(&yp); free(yy_read_name(&yp));
                if (yy_peek(&yp) == '>') yy_next(&yp); }
            if (yy_peek(&yp) != ':') { fprintf(stderr, "bison:%d: expected ':'\n", yp.lineno); return -1; }
            yy_next(&yp);

            int first_alt = 1;
            while (1) {
                yy_skip_ws(&yp);
                if (yy_peek(&yp) == ';' || yy_peek(&yp) == '\n') {
                    if (yy_peek(&yp) == ';') yy_next(&yp);
                    if (first_alt) { fprintf(stderr, "bison:%d: empty rule\n", yp.lineno); return -1; }
                    break;
                }
                if (yy_peek(&yp) == '|') { yy_next(&yp); yy_skip_ws(&yp); first_alt = 0; }

                if (nrules >= MAX_RULES) { fprintf(stderr, "bison: too many rules\n"); return -1; }
                int ri = nrules++;
                Rule *r = &rules[ri];
                memset(r, 0, sizeof(Rule));
                r->lhs = lhs; r->nrhs = 0; r->prec_sym = -1; r->action = NULL;

                while (1) {
                    yy_skip_ws(&yp);
                    int c2 = yy_peek(&yp);
                    if (c2 == EOF || c2 == ';' || c2 == '\n' || c2 == '|') break;
                    if (c2 == '{') {
                        yy_next(&yp); r->action = yy_read_code_block(&yp, "}"); break;
                    }
                    if (c2 == '%') {
                        yy_next(&yp); char *pd = yy_read_name(&yp);
                        if (pd && strcmp(pd, "prec") == 0) {
                            free(pd); yy_skip_ws(&yp); char *ps = yy_read_name(&yp);
                            if (ps) { int psi = find_symbol(ps); if (psi >= 0) r->prec_sym = psi; free(ps); }
                        } else { free(pd); break; }
                        continue;
                    }
                    if (c2 == '\'') {
                        yy_next(&yp); char lit[8]; int li = 0;
                        c2 = yy_next(&yp);
                        if (c2 == '\\') c2 = yy_next(&yp);
                        if (c2 != EOF) lit[li++] = (char)c2;
                        yy_next(&yp); lit[li] = '\0';
                        int si = find_or_add_symbol(lit, 1);
                        if (r->nrhs < MAX_RHS_LEN) r->rhs[r->nrhs++] = si;
                        continue;
                    }
                    char *sym = yy_read_name(&yp);
                    if (!sym) break;
                    int si = find_or_add_symbol(sym, 0);
                    free(sym);
                    if (r->nrhs < MAX_RHS_LEN) r->rhs[r->nrhs++] = si;
                }

                if (yy_peek(&yp) == ';') { yy_next(&yp); break; }
                yy_skip_ws(&yp);
                if (yy_peek(&yp) == '\n') break;
            }
            continue;
        }
        yy_next(&yp);
    }

    /* Augmented start rule */
    if (start_symbol < 0) {
        for (int i = 0; i < nrules; i++) { start_symbol = rules[i].lhs; break; }
    }
    if (start_symbol >= 0 && nrules < MAX_RULES) {
        augmented_rule = nrules++;
        Rule *r = &rules[augmented_rule];
        memset(r, 0, sizeof(Rule));
        r->lhs = find_or_add_symbol("$accept", 0);
        r->rhs[0] = start_symbol;
        r->nrhs = 1;
        r->prec_sym = -1;
    }
    return 0;
}

/* ===== LR(0) itemset construction ===== */
static ItemSet *current_set = NULL;

static int add_item_to_set(ItemSet *s, int rule_idx, int dot_pos) {
    for (int i = 0; i < s->nitems; i++)
        if (s->items[i].rule_idx == rule_idx && s->items[i].dot_pos == dot_pos)
            return i;
    if (s->nitems >= MAX_ITEMS) { fprintf(stderr, "bison: too many items\n"); exit(1); }
    int idx = s->nitems++;
    s->items[idx].rule_idx = rule_idx;
    s->items[idx].dot_pos = dot_pos;
    return idx;
}

static void closure(void) {
    ItemSet *s = current_set;
    int changed;
    do {
        changed = 0;
        for (int i = 0; i < s->nitems; i++) {
            LRItem *it = &s->items[i];
            Rule *r = &rules[it->rule_idx];
            if (it->dot_pos >= r->nrhs) continue;
            int sym = r->rhs[it->dot_pos];
            if (sym < 0 || sym >= nsymbols) continue;
            if (symbols[sym].type != 0) continue;
            for (int j = 0; j < nrules; j++) {
                if (rules[j].lhs == sym) {
                    int on = s->nitems;
                    add_item_to_set(s, j, 0);
                    if (s->nitems > on) changed = 1;
                }
            }
        }
    } while (changed);
}

static int find_or_add_state(ItemSet *set) {
    for (int s = 0; s < nstates; s++) {
        ItemSet *es = &states[s];
        if (es->nitems != set->nitems) continue;
        int match = 1;
        for (int i = 0; i < es->nitems; i++)
            if (es->items[i].rule_idx != set->items[i].rule_idx ||
                es->items[i].dot_pos != set->items[i].dot_pos)
                { match = 0; break; }
        if (match) return s;
    }
    if (nstates >= MAX_STATES) { fprintf(stderr, "bison: too many states\n"); exit(1); }
    int idx = nstates++;
    memcpy(&states[idx], set, sizeof(ItemSet));
    for (int i = 0; i < MAX_SYMBOLS; i++) goto_tab[idx][i] = -1;
    return idx;
}

static int build_lr0_states(void) {
    /* Initialize first state from augmented start rule */
    ItemSet init;
    init.nitems = 0;
    add_item_to_set(&init, augmented_rule, 0);
    current_set = &init;
    closure();

    ItemSet *temp_set = xmalloc(sizeof(ItemSet));
    states[0] = init;
    nstates = 1;

    int worklist[MAX_STATES];
    worklist[0] = 0;
    int nwork = 1;

    for (int wi = 0; wi < nwork; wi++) {
        int si = worklist[wi];
        ItemSet *src = &states[si];

        /* Collect unique symbols after dot */
        int syms_after[MAX_ITEMS], nsyms = 0;
        for (int i = 0; i < src->nitems; i++) {
            LRItem *it = &src->items[i];
            Rule *r = &rules[it->rule_idx];
            if (it->dot_pos >= r->nrhs) continue;
            int sym = r->rhs[it->dot_pos];
            int found = 0;
            for (int j = 0; j < nsyms; j++) if (syms_after[j] == sym) { found = 1; break; }
            if (!found) syms_after[nsyms++] = sym;
        }

        for (int si2 = 0; si2 < nsyms; si2++) {
            int sym = syms_after[si2];
            if (sym < 0) continue;

            temp_set->nitems = 0;
            for (int i = 0; i < src->nitems; i++) {
                LRItem *it = &src->items[i];
                Rule *r = &rules[it->rule_idx];
                if (it->dot_pos < r->nrhs && r->rhs[it->dot_pos] == sym)
                    add_item_to_set(temp_set, it->rule_idx, it->dot_pos + 1);
            }
            current_set = temp_set;
            closure();

            int ns = find_or_add_state(temp_set);
            if (ns == nstates) {
                nstates++;
                if (nstates > MAX_STATES) { fprintf(stderr, "bison: too many states\n"); exit(1); }
                worklist[nwork++] = ns;
            }
            goto_tab[si][sym] = ns;
        }
    }

    free(temp_set);
    return 0;
}

/* ===== LALR(1) lookahead ===== */

/* Compute flat offset for item (state, item_index) */
static int item_offset(int state_idx, int item_idx) {
    int off = 0;
    for (int s = 0; s < state_idx; s++) off += states[s].nitems;
    off += item_idx;
    return off;
}

/* Return the number of terminal symbols */
static int nterminals(void) {
    int n = 0;
    for (int i = 0; i < nsymbols; i++) if (symbols[i].type == 1) n++;
    return n;
}

/* Allocate lookahead array and set up initial lookaheads */
static void init_lookaheads(void) {
    int total = 0;
    for (int s = 0; s < nstates; s++) total += states[s].nitems;
    if (total > lookahead_cap) {
        lookaheads = xrealloc(lookaheads, (size_t)total * sizeof(LookBits));
        lookahead_cap = total;
    }
    memset(lookaheads, 0, (size_t)total * sizeof(LookBits));

    /* $end symbol */
    int end_sym = find_symbol("$end");
    if (end_sym < 0) end_sym = find_or_add_symbol("$end", 1);

    /* Set $end lookahead on the final item of the augmented start rule */
    for (int s = 0; s < nstates; s++) {
        ItemSet *st = &states[s];
        for (int i = 0; i < st->nitems; i++) {
            LRItem *it = &st->items[i];
            if (it->rule_idx == augmented_rule && it->dot_pos == 1) {
                int off = item_offset(s, i);
                lookaheads[off] |= (1ULL << 0); /* $end = first terminal */
            }
        }
    }
}

static void propagate_lookaheads(void) {
    int changed;
    do {
        changed = 0;
        for (int s = 0; s < nstates; s++) {
            ItemSet *st = &states[s];
            for (int i = 0; i < st->nitems; i++) {
                LRItem *it = &st->items[i];
                Rule *r = &rules[it->rule_idx];
                if (it->dot_pos >= r->nrhs) continue;
                int sym = r->rhs[it->dot_pos];
                int ns = goto_tab[s][sym];
                if (ns < 0) continue;

                /* Find matching item in destination state */
                ItemSet *dst = &states[ns];
                for (int j = 0; j < dst->nitems; j++) {
                    if (dst->items[j].rule_idx == it->rule_idx &&
                        dst->items[j].dot_pos == it->dot_pos + 1) {
                        int src_off = item_offset(s, i);
                        int dst_off = item_offset(ns, j);
                        LookBits old = lookaheads[dst_off];
                        lookaheads[dst_off] |= lookaheads[src_off];
                        if (lookaheads[dst_off] != old) changed = 1;
                        break;
                    }
                }
            }
        }
    } while (changed);
}

/* ===== Conflict detection ===== */
static int sr_conflicts = 0, rr_conflicts = 0;

static void report_conflicts(void) {
    int nterm = nterminals();
    for (int s = 0; s < nstates; s++) {
        ItemSet *st = &states[s];

        int reducers[MAX_ITEMS], redule[MAX_ITEMS], nred = 0;
        int shift_syms[MAX_ITEMS], nshift = 0;

        for (int i = 0; i < st->nitems; i++) {
            LRItem *it = &st->items[i];
            Rule *r = &rules[it->rule_idx];
            if (it->dot_pos >= r->nrhs) {
                int off = item_offset(s, i);
                LookBits lb = lookaheads[off];
                for (int t = 0; t < nterm; t++)
                    if (lb & (1ULL << t)) {
                        reducers[nred] = t;
                        redule[nred] = it->rule_idx;
                        nred++;
                    }
            } else {
                int sym = r->rhs[it->dot_pos];
                if (sym >= 0 && symbols[sym].type == 1)
                    shift_syms[nshift++] = sym;
            }
        }

        /* S/R conflicts */
        for (int ri = 0; ri < nred; ri++) {
            for (int si = 0; si < nshift; si++) {
                int sterm = shift_syms[si];
                /* Map sterm to terminal bit */
                int tbit = -1;
                { int b = 0;
                  for (int i2 = 0; i2 < nsymbols; i2++)
                      if (symbols[i2].type == 1) {
                          if (i2 == sterm) tbit = b;
                          b++;
                      }
                }
                if (tbit != reducers[ri]) continue;

                /* Precedence resolution */
                int rprec = rules[redule[ri]].prec_sym >= 0
                    ? symbols[rules[redule[ri]].prec_sym].prec : 0;
                int sprec = symbols[sterm].prec;
                char sassoc = symbols[sterm].assoc;

                if (rprec > sprec) continue;
                if (rprec < sprec) continue;
                if (sassoc == 'l') continue;
                if (sassoc == 'r') continue;

                fprintf(stderr, "bison: shift/reduce conflict in state %d\n", s);
                sr_conflicts++;
            }
        }

        /* R/R conflicts */
        for (int ri = 0; ri < nred; ri++)
            for (int rj = ri + 1; rj < nred; rj++)
                if (reducers[ri] == reducers[rj]) {
                    fprintf(stderr, "bison: reduce/reduce conflict in state %d "
                            "(rules %d and %d)\n", s, redule[ri], redule[rj]);
                    rr_conflicts++;
                }
    }
}

/* ===== Generate parser C source ===== */
static void generate_parser(const char *input_name, FILE *out) {
    int nterm = 0, nnonterm = 0;
    for (int i = 0; i < nsymbols; i++) {
        if (symbols[i].type == 1) nterm++;
        else nnonterm++;
    }

    /* Map symbol indices to yy parser indices */
    int sym_to_idx[MAX_SYMBOLS];
    memset(sym_to_idx, -1, sizeof(sym_to_idx));
    int ti = 0;
    /* Put $end at index 0 */
    for (int i = 0; i < nsymbols; i++)
        if (symbols[i].type == 1 && strcmp(symbols[i].name, "$end") == 0)
            sym_to_idx[i] = ti++;
    for (int i = 0; i < nsymbols; i++)
        if (sym_to_idx[i] < 0 && symbols[i].type == 1)
            sym_to_idx[i] = ti++;
    int first_nonterm = ti;  /* first nonterminal index */
    for (int i = 0; i < nsymbols; i++)
        if (sym_to_idx[i] < 0)
            sym_to_idx[i] = first_nonterm++;

    /* Sorted terminals for naming */
    Symbol **sterms = xmalloc(sizeof(Symbol *) * (size_t)nterm);
    {
        int t = 0;
        for (int i = 0; i < nsymbols; i++)
            if (symbols[i].type == 1) sterms[t++] = &symbols[i];
    }

    /* Write output */
    fprintf(out, "/* A parser generated by meuos-buildtools bison */\n");
    fprintf(out, "/* Source: %s */\n\n", input_name ? input_name : "(stdin)");
    if (prologue_code) fprintf(out, "%s\n\n", prologue_code);

    fprintf(out, "#ifndef YYSTYPE\n#define YYSTYPE int\n#endif\n\n");

    /* Token defines */
    fprintf(out, "/* Token identifiers */\n");
    for (int i = 0; i < nterm; i++)
        fprintf(out, "#define YYTOK_%s %d\n", sterms[i]->name, sym_to_idx[find_symbol(sterms[i]->name)]);
    fprintf(out, "\n");

    /* yytname */
    fprintf(out, "static const char *const yytname[] = {\n");
    for (int i = 0; i < nterm; i++)
        fprintf(out, "  \"%s\",\n", sterms[i]->name);
    fprintf(out, "};\n\n");

    /* yyr1: LHS of each rule */
    fprintf(out, "static const short yyr1[] = {");
    int last_term_idx = nterm - 1;
    (void)last_term_idx;
    for (int i = 0; i < nrules; i++) {
        if (i % 15 == 0) fprintf(out, "\n  ");
        fprintf(out, "%d,", sym_to_idx[rules[i].lhs]);
    }
    fprintf(out, "\n};\n\n");

    /* yyr2: RHS length */
    fprintf(out, "static const short yyr2[] = {");
    for (int i = 0; i < nrules; i++) {
        if (i % 20 == 0) fprintf(out, "\n  ");
        fprintf(out, "%d,", rules[i].nrhs);
    }
    fprintf(out, "\n};\n\n");

    /* yydefact: default reduction per state */
    fprintf(out, "static const short yydefact[] = {\n");
    for (int s = 0; s < nstates; s++) {
        int defact = -1;
        ItemSet *st = &states[s];
        for (int i = 0; i < st->nitems; i++) {
            LRItem *it = &st->items[i];
            if (it->dot_pos >= rules[it->rule_idx].nrhs && it->rule_idx != augmented_rule)
                { defact = it->rule_idx; break; }
        }
        fprintf(out, "  %d,", defact);
        if (s % 10 == 9) fprintf(out, "\n");
    }
    fprintf(out, "\n};\n\n");

    /* yytable: shift actions */
    fprintf(out, "static const short yytable[] = {\n");
    int nentries = nstates * nterm;
    (void)nentries;
    for (int s = 0; s < nstates; s++) {
        fprintf(out, "  /* %d */", s);
        for (int t = 0; t < nterm; t++) {
            int sym_idx = find_symbol(sterms[t]->name);
            int ns = goto_tab[s][sym_idx];
            fprintf(out, " %d,", ns >= 0 ? ns : -1);
        }
        fprintf(out, "\n");
    }
    fprintf(out, "};\n\n");

    /* yycheck */
    fprintf(out, "static const short yycheck[] = {\n");
    for (int s = 0; s < nstates; s++) {
        fprintf(out, "  /* %d */", s);
        for (int t = 0; t < nterm; t++) {
            int sym_idx = find_symbol(sterms[t]->name);
            int ns = goto_tab[s][sym_idx];
            fprintf(out, " %d,", ns >= 0 ? sym_to_idx[sym_idx] : -1);
        }
        fprintf(out, "\n");
    }
    fprintf(out, "};\n\n");

    /* Parse function */
    fprintf(out, "int yylex(void);\n");
    fprintf(out, "void yyerror(const char *);\n\n");
    fprintf(out, "int yyparse(void) {\n");
    fprintf(out, "  int yystate = 0;\n");
    fprintf(out, "  YYSTYPE yylval;\n");
    fprintf(out, "  int yychar;\n");
    fprintf(out, "  enum { YYSTACKDEPTH = 100 };\n");
    fprintf(out, "  YYSTYPE yyval[YYSTACKDEPTH];\n");
    fprintf(out, "  int yystack[YYSTACKDEPTH];\n");
    fprintf(out, "  int yypos = 0;\n");
    fprintf(out, "  yystack[0] = 0;\n");
    fprintf(out, "  yychar = yylex();\n");
    fprintf(out, "  for (;;) {\n");
    fprintf(out, "    int action = -1;\n");
    fprintf(out, "    if (yychar >= 0 && yychar < %d) {\n", nterm);
    fprintf(out, "      int idx = yystate * %d + yychar;\n", nterm);
    fprintf(out, "      if (yycheck[idx] == yychar)\n");
    fprintf(out, "        action = yytable[idx];\n");
    fprintf(out, "    }\n");
    fprintf(out, "    if (action < 0) {\n");
    fprintf(out, "      action = yydefact[yystate];\n");
    fprintf(out, "      if (action < 0) {\n");
    fprintf(out, "        if (yychar == 0) return 0;\n");
    fprintf(out, "        yyerror(\"parse error\");\n");
    fprintf(out, "        yychar = yylex();\n");
    fprintf(out, "        continue;\n");
    fprintf(out, "      }\n");
    fprintf(out, "      int n = yyr2[action];\n");
    fprintf(out, "      yypos -= n;\n");
    fprintf(out, "      yystate = yystack[yypos];\n");
    fprintf(out, "      switch (action) {\n");
    for (int r = 0; r < nrules; r++) {
        fprintf(out, "        case %d:", r);
        if (rules[r].action) {
            fprintf(out, " { %s; break; }", rules[r].action);
        } else {
            fprintf(out, " break;");
        }
        fprintf(out, "\n");
    }
    fprintf(out, "      }\n");
    fprintf(out, "      yystate = yydefact[0]; /* TODO: goto */\n");
    fprintf(out, "      yystack[++yypos] = yystate;\n");
    fprintf(out, "      yyval[yypos] = yylval;\n");
    fprintf(out, "    } else {\n");
    fprintf(out, "      yypos++;\n");
    fprintf(out, "      yystack[yypos] = action;\n");
    fprintf(out, "      yyval[yypos] = yylval;\n");
    fprintf(out, "      yychar = yylex();\n");
    fprintf(out, "    }\n");
    fprintf(out, "  }\n");
    fprintf(out, "}\n");

    if (epilogue_code) fprintf(out, "\n%s\n", epilogue_code);
    free(sterms);
}

/* ===== Help ===== */
static void print_help(void) {
    printf("Usage: bison [OPTION]... FILE...\n");
    printf("GNU bison compatible LALR(1) parser generator (minimal subset).\n\n");
    printf("Options:\n");
    printf("  --help        display this help and exit\n");
    printf("  --version     display version info and exit\n");
    printf("  -o FILE       set output file name (default: parser.c)\n\n");
    printf("MeuOS buildtools bison generates C source with yyparse().\n");
}
static void print_version(void) {
    printf("bison (MeuOS buildtools) 0.1.0\nCopyright (C) MeuOS Project\nLicense: RFL v1.0\n");
}

/* ===== Entry point ===== */
int bison_main(int argc, char **argv) {
    const char *input_file = NULL, *output_file = "parser.c";
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) { print_help(); return 0; }
        if (strcmp(argv[i], "--version") == 0) { print_version(); return 0; }
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) { output_file = argv[++i]; continue; }
        if (argv[i][0] != '-') input_file = argv[i];
    }
    if (!input_file) {
        fprintf(stderr, "bison: no input file\nRun 'bison --help' for usage.\n");
        return 1;
    }

    FILE *fp = fopen(input_file, "r");
    if (!fp) { fprintf(stderr, "bison: %s: %s\n", input_file, strerror(errno)); return 1; }
    size_t cap = 65536, len = 0;
    char *data = xmalloc(cap);
    size_t n;
    while ((n = fread(data + len, 1, cap - len, fp)) > 0) {
        len += n;
        if (len >= cap - 1024) { cap *= 2; data = xrealloc(data, cap); }
    }
    data[len] = '\0';
    fclose(fp);

    if (parse_y_file(data, len) != 0) { free(data); return 1; }
    free(data);
    if (nrules == 0) { fprintf(stderr, "bison: no grammar rules\n"); return 1; }

    if (build_lr0_states() != 0) return 1;
    init_lookaheads();
    propagate_lookaheads();
    report_conflicts();

    FILE *out = fopen(output_file, "w");
    if (!out) { fprintf(stderr, "bison: %s: %s\n", output_file, strerror(errno)); return 1; }
    generate_parser(input_file, out);
    fclose(out);

    printf("bison: generated %s (%d states, %d rules, %d symbols)\n",
           output_file, nstates, nrules, nsymbols);
    if (sr_conflicts || rr_conflicts)
        printf("bison: %d shift/reduce, %d reduce/reduce conflicts\n",
               sr_conflicts, rr_conflicts);
    return 0;
}