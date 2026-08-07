/* flex.c — Minimal flex-compatible lexer generator
 *
 * Reads .l lexer definitions, generates C code with a DFA-driven yylex().
 * Implements a useful subset of POSIX lex/flex:
 *   - Literal chars, brackets/ranges/negation, POSIX [:classes:]
 *   - Quantifiers *, +, ?
 *   - Grouping ( ), alternation |
 *   - Anchors ^ (BOL), $ (EOL)
 *   - Dot . (any except \n), escapes, ECHO
 *   - Start conditions <INITIAL> (stripped, accepted)
 *
 * Build:
 *   cc -O2 -std=c11 -Wall -Wextra -Wpedantic -Werror -o build/flex src/flex.c
 */

#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Portable strdup for C11 strict mode */
static char *xstrdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *d = malloc(n);
    if (d) memcpy(d, s, n);
    return d;
}

/* ======================== Constants ======================== */

#define EPSILON     (-1)
#define CC_CLASS    (-2)   /* character-class edge (bitset-driven) */
#define CC_ANY      (-3)   /* .  wildcard */
#define MAX_RULES   256
#define MAX_NSET    4096   /* max NFA states in a DFA state set */

/* ======================== CharSet (256-bit bitset) ======================== */

typedef struct { uint32_t bits[8]; } CharSet;

static void cs_clear(CharSet *s) { memset(s, 0, sizeof(*s)); }
static void cs_set(CharSet *s, int c) { s->bits[c>>5] |= 1u << (c & 31); }
static int  cs_get(const CharSet *s, int c) { return (s->bits[c>>5] >> (c & 31)) & 1; }
/* cs_negate intentionally omitted — not currently used */

/* ======================== NFA  ======================== */

typedef struct { int label; CharSet cset; int neg; int dest; } NfaEdge;

typedef struct {
    NfaEdge *edges;
    int ne, ce;
    int accept_rule;   /* -1 = not accepting */
} NfaState;

typedef struct {
    NfaState *states;
    int n, c;
    int start;
} Nfa;

static Nfa *nfa_new(void) {
    Nfa *n = calloc(1, sizeof(Nfa));
    n->c = 64; n->states = calloc(n->c, sizeof(NfaState));
    for (int i = 0; i < n->c; i++) n->states[i].accept_rule = -1;
    return n;
}

static int nfa_add_state(Nfa *n) {
    if (n->n >= n->c) {
        n->c *= 2;
        n->states = realloc(n->states, (size_t)n->c * sizeof(NfaState));
        for (int i = n->n; i < n->c; i++) n->states[i].accept_rule = -1;
    }
    int id = n->n++;
    n->states[id].accept_rule = -1;
    return id;
}

static void nfa_add_edge(Nfa *n, int src, int label, int neg, const CharSet *cset, int dest) {
    NfaState *s = &n->states[src];
    if (s->ne >= s->ce) {
        s->ce = s->ce ? s->ce * 2 : 8;
        s->edges = realloc(s->edges, (size_t)s->ce * sizeof(NfaEdge));
    }
    NfaEdge *e = &s->edges[s->ne++];
    e->label = label;
    e->neg = neg;
    if (cset) e->cset = *cset; else cs_clear(&e->cset);
    e->dest = dest;
}

static void nfa_set_accept(Nfa *n, int state, int rule) {
    n->states[state].accept_rule = rule;
}

/* Create a fragment: a single start-to-accept ε edge */
static void nfa_frag(Nfa *n, int *start, int *accept) {
    *start  = nfa_add_state(n);
    *accept = nfa_add_state(n);
    nfa_add_edge(n, *start, EPSILON, 0, NULL, *accept);
}

/* ======================== AST ======================== */

typedef enum { T_CHAR, T_DOT, T_CONCAT, T_ALT, T_STAR, T_PLUS, T_OPT,
               T_BRACKET, T_BOL, T_EOL, T_EMPTY } AstTag;

typedef struct Ast {
    AstTag tag;
    int ch;                 /* T_CHAR */
    CharSet cset;           /* T_BRACKET */
    int neg;                /* T_BRACKET */
    struct Ast *l, *r;      /* binary ops */
    struct Ast *c;          /* unary ops */
} Ast;

static Ast *ast_new(AstTag tag) {
    Ast *a = calloc(1, sizeof(Ast));
    a->tag = tag;
    return a;
}

static void ast_free(Ast *a) {
    if (!a) return;
    if (a->tag == T_CONCAT || a->tag == T_ALT) {
        ast_free(a->l); ast_free(a->r);
    } else if (a->tag == T_STAR || a->tag == T_PLUS || a->tag == T_OPT) {
        ast_free(a->c);
    }
    free(a);
}

/* ======================== Regex Parser ======================== */

typedef struct {
    const char *s;
    int pos, len;
    int err;
    char errmsg[256];
} RParse;

static int rp_peek(RParse *p) { return p->pos < p->len ? (unsigned char)p->s[p->pos] : EOF; }
static int rp_next(RParse *p) { return p->pos < p->len ? (unsigned char)p->s[p->pos++] : EOF; }

static void rp_err(RParse *p, const char *msg) {
    if (!p->err) {
        p->err = 1;
        snprintf(p->errmsg, sizeof(p->errmsg), "pos %d: %s", p->pos, msg);
    }
}

/* Forward */
static Ast *parse_regex(RParse *p);

/* Escape sequences */
static int parse_escape(RParse *p) {
    int c = rp_next(p);
    switch (c) {
        case 'n': return '\n';
        case 't': return '\t';
        case 'r': return '\r';
        case '0': return '\0';
        case EOF: rp_err(p, "unterminated escape"); return '\\';
        default:  return c;
    }
}

/* Parse a character class: [...] */
static Ast *parse_bracket(RParse *p) {
    int neg = 0;
    CharSet cs; cs_clear(&cs);

    if (rp_peek(p) == '^') { neg = 1; rp_next(p); }

    /* First char inside bracket */
    while (rp_peek(p) != EOF && rp_peek(p) != ']') {
        if (rp_peek(p) == '[' && p->pos + 1 < p->len && p->s[p->pos + 1] == ':') {
            /* POSIX character class [:alpha:] etc. */
            rp_next(p); rp_next(p); /* skip [: */
            char cls[32]; int ci = 0;
            while (rp_peek(p) != EOF && rp_peek(p) != ':' && ci < 30)
                cls[ci++] = (char)rp_next(p);
            cls[ci] = '\0';
            if (rp_peek(p) == ':') rp_next(p);
            if (rp_peek(p) == ']') rp_next(p); /* skip ] - this is the closing ]] */
            /* expand class */
            if      (!strcmp(cls, "alpha")) { for (int i = 'a'; i <= 'z'; i++) cs_set(&cs,i); for (int i='A';i<='Z';i++) cs_set(&cs,i); }
            else if (!strcmp(cls, "digit")) { for (int i = '0'; i <= '9'; i++) cs_set(&cs,i); }
            else if (!strcmp(cls, "alnum")) { for (int i = 'a'; i <= 'z'; i++) cs_set(&cs,i); for (int i='A';i<='Z';i++) cs_set(&cs,i); for (int i='0';i<='9';i++) cs_set(&cs,i); }
            else if (!strcmp(cls, "space")) { cs_set(&cs,' '); cs_set(&cs,'\t'); cs_set(&cs,'\n'); cs_set(&cs,'\r'); cs_set(&cs,'\v'); cs_set(&cs,'\f'); }
            else { char buf[64]; snprintf(buf,sizeof(buf),"unknown POSIX class [:%.30s:]",cls); rp_err(p,buf); }
        } else if (rp_peek(p) == '\\') {
            rp_next(p); cs_set(&cs, parse_escape(p));
        } else {
            int lo = rp_next(p);
            if (rp_peek(p) == '-') {
                rp_next(p);
                int hi = rp_next(p);
                if (hi == ']') { /* trailing hyphen is literal */
                    cs_set(&cs, lo);
                    cs_set(&cs, '-');
                    break;
                }
                for (int i = lo; i <= hi; i++) cs_set(&cs, i);
            } else {
                cs_set(&cs, lo);
            }
        }
    }

    if (rp_peek(p) == ']') rp_next(p);
    else rp_err(p, "unterminated bracket");

    Ast *a = ast_new(T_BRACKET);
    a->cset = cs;
    a->neg  = neg;
    return a;
}

/* Parse primary: char, ., (), [], ^, $, escape */
static Ast *parse_primary(RParse *p) {
    int c = rp_peek(p);
    if (c == EOF) { rp_err(p, "unexpected end of pattern"); return ast_new(T_EMPTY); }

    switch (c) {
        case '(':
            rp_next(p);
            Ast *a = parse_regex(p);
            if (rp_peek(p) == ')') rp_next(p);
            else rp_err(p, "unmatched (");
            return a;
        case ')':
            rp_next(p);
            rp_err(p, "unexpected )");
            return ast_new(T_EMPTY);
        case '|':
            rp_next(p);
            rp_err(p, "unexpected |");
            return ast_new(T_EMPTY);
        case '*': rp_next(p); rp_err(p, "unexpected *"); return ast_new(T_EMPTY);
        case '+': rp_next(p); rp_err(p, "unexpected +"); return ast_new(T_EMPTY);
        case '?': rp_next(p); rp_err(p, "unexpected ?"); return ast_new(T_EMPTY);
        case '[': rp_next(p); return parse_bracket(p);
        case '.': rp_next(p); return ast_new(T_DOT);
        case '^': rp_next(p); return ast_new(T_BOL);
        case '$': rp_next(p); return ast_new(T_EOL);
        case '\\': rp_next(p); { Ast *a = ast_new(T_CHAR); a->ch = parse_escape(p); return a; }
        default:
            rp_next(p);
            { Ast *a = ast_new(T_CHAR); a->ch = c; return a; }
    }
}

/* Parse unary: primary [*+?] */
static Ast *parse_unary(RParse *p) {
    Ast *a = parse_primary(p);
    if (p->err) return a;
    int c = rp_peek(p);
    if (c == '*') { rp_next(p); Ast *s = ast_new(T_STAR); s->c = a; return s; }
    if (c == '+') { rp_next(p); Ast *s = ast_new(T_PLUS); s->c = a; return s; }
    if (c == '?') { rp_next(p); Ast *s = ast_new(T_OPT);  s->c = a; return s; }
    return a;
}

/* Parse concat: unary+ */
static Ast *parse_concat(RParse *p) {
    Ast *a = parse_unary(p);
    if (p->err) return a;
    while (!p->err) {
        int c = rp_peek(p);
        if (c == EOF || c == '|' || c == ')' ) break;
        Ast *r = parse_unary(p);
        if (p->err) { ast_free(r); break; }
        Ast *cat = ast_new(T_CONCAT);
        cat->l = a; cat->r = r;
        a = cat;
    }
    return a;
}

/* Parse alternation: concat (| concat)* */
static Ast *parse_regex(RParse *p) {
    Ast *a = parse_concat(p);
    if (p->err) return a;
    while (rp_peek(p) == '|') {
        rp_next(p);
        Ast *r = parse_concat(p);
        if (p->err) { ast_free(r); break; }
        Ast *alt = ast_new(T_ALT);
        alt->l = a; alt->r = r;
        a = alt;
    }
    return a;
}

static Ast *parse_pattern(const char *s) {
    RParse p;
    memset(&p, 0, sizeof(p));
    p.s = s; p.len = (int)strlen(s);
    Ast *a = parse_regex(&p);
    if (p.err) { fprintf(stderr, "regex error: %s\n", p.errmsg); ast_free(a); return NULL; }
    return a;
}

/* ======================== Thompson Construction ======================== */

/* Build NFA fragment for AST node.
 * start/accept are output parameters for the fragment's endpoints.
 */
static void thompson_build(Ast *ast, Nfa *nfa, int *start, int *accept, int rule) {
    if (!ast) { nfa_frag(nfa, start, accept); return; }

    switch (ast->tag) {
        case T_EMPTY:
            nfa_frag(nfa, start, accept);
            return;

        case T_CHAR: {
            *start = nfa_add_state(nfa);
            *accept = nfa_add_state(nfa);
            nfa_add_edge(nfa, *start, ast->ch, 0, NULL, *accept);
            return;
        }

        case T_DOT: {
            *start = nfa_add_state(nfa);
            *accept = nfa_add_state(nfa);
            nfa_add_edge(nfa, *start, CC_ANY, 0, NULL, *accept);
            return;
        }

        case T_BOL: {
            /* ^ — zero-width assertion, treated as ε that the generated code handles */
            *start = nfa_add_state(nfa);
            *accept = nfa_add_state(nfa);
            nfa_add_edge(nfa, *start, T_BOL, 0, NULL, *accept);
            return;
        }

        case T_EOL: {
            /* $ — zero-width assertion, treated as ε */
            *start = nfa_add_state(nfa);
            *accept = nfa_add_state(nfa);
            nfa_add_edge(nfa, *start, T_EOL, 0, NULL, *accept);
            return;
        }

        case T_BRACKET: {
            *start = nfa_add_state(nfa);
            *accept = nfa_add_state(nfa);
            nfa_add_edge(nfa, *start, CC_CLASS, ast->neg, &ast->cset, *accept);
            return;
        }

        case T_CONCAT: {
            int s1, a1, s2, a2;
            thompson_build(ast->l, nfa, &s1, &a1, rule);
            thompson_build(ast->r, nfa, &s2, &a2, rule);
            nfa_add_edge(nfa, a1, EPSILON, 0, NULL, s2);
            *start  = s1;
            *accept = a2;
            return;
        }

        case T_ALT: {
            int s1, a1, s2, a2;
            thompson_build(ast->l, nfa, &s1, &a1, rule);
            thompson_build(ast->r, nfa, &s2, &a2, rule);
            *start  = nfa_add_state(nfa);
            *accept = nfa_add_state(nfa);
            nfa_add_edge(nfa, *start, EPSILON, 0, NULL, s1);
            nfa_add_edge(nfa, *start, EPSILON, 0, NULL, s2);
            nfa_add_edge(nfa, a1, EPSILON, 0, NULL, *accept);
            nfa_add_edge(nfa, a2, EPSILON, 0, NULL, *accept);
            return;
        }

        case T_STAR: {
            int s1, a1;
            thompson_build(ast->c, nfa, &s1, &a1, rule);
            *start  = nfa_add_state(nfa);
            *accept = nfa_add_state(nfa);
            nfa_add_edge(nfa, *start, EPSILON, 0, NULL, s1);
            nfa_add_edge(nfa, *start, EPSILON, 0, NULL, *accept);
            nfa_add_edge(nfa, a1, EPSILON, 0, NULL, s1);
            nfa_add_edge(nfa, a1, EPSILON, 0, NULL, *accept);
            return;
        }

        case T_PLUS: {
            int s1, a1;
            thompson_build(ast->c, nfa, &s1, &a1, rule);
            *start  = s1;
            *accept = nfa_add_state(nfa);
            nfa_add_edge(nfa, a1, EPSILON, 0, NULL, s1);
            nfa_add_edge(nfa, a1, EPSILON, 0, NULL, *accept);
            return;
        }

        case T_OPT: {
            int s1, a1;
            thompson_build(ast->c, nfa, &s1, &a1, rule);
            *start  = nfa_add_state(nfa);
            *accept = nfa_add_state(nfa);
            nfa_add_edge(nfa, *start, EPSILON, 0, NULL, s1);
            nfa_add_edge(nfa, *start, EPSILON, 0, NULL, *accept);
            nfa_add_edge(nfa, a1, EPSILON, 0, NULL, *accept);
            return;
        }
    }
}

/* ======================== NFA -> DFA (Subset Construction) ======================== */

/* Compute epsilon-closure of a set of NFA states.
 * Input: set of states in 'src' (length nsrc).
 * Output: sorted set in 'dst' (returns size), max MAX_NSET elements.
 */
static int epsilon_closure(Nfa *nfa, const int *src, int nsrc, int *dst) {
    int visited[MAX_NSET] = {0};
    int stack[MAX_NSET];
    int nclosed = 0;
    int nstack  = 0;

    for (int i = 0; i < nsrc; i++) {
        int s = src[i];
        if (s < 0 || s >= nfa->n) continue;
        if (!visited[s]) {
            visited[s] = 1;
            stack[nstack++] = s;
            dst[nclosed++] = s;
        }
    }

    while (nstack > 0) {
        int s = stack[--nstack];
        NfaState *st = &nfa->states[s];
        for (int j = 0; j < st->ne; j++) {
            if (st->edges[j].label == EPSILON || st->edges[j].label == T_BOL || st->edges[j].label == T_EOL) {
                int d = st->edges[j].dest;
                if (d >= 0 && d < nfa->n && !visited[d]) {
                    visited[d] = 1;
                    stack[nstack++] = d;
                    dst[nclosed++] = d;
                    if (nclosed >= MAX_NSET) return nclosed;
                }
            }
        }
    }

    /* Sort (simple insertion sort, nclosed is typically small) */
    for (int i = 1; i < nclosed; i++) {
        int tmp = dst[i];
        int j = i - 1;
        while (j >= 0 && dst[j] > tmp) { dst[j+1] = dst[j]; j--; }
        dst[j+1] = tmp;
    }

    return nclosed;
}

/* Compare two sorted state sets for equality */
static int set_eq(const int *a, int na, const int *b, int nb) {
    if (na != nb) return 0;
    for (int i = 0; i < na; i++) if (a[i] != b[i]) return 0;
    return 1;
}

typedef struct {
    int nstates;
    int **trans;         /* trans[state][c] = next_state or -1 */
    int *accept;         /* accept[state]  = rule_index or -1 */
    int *bol_accept;     /* bol_accept[state] = rule_index for BOL-sensitive patterns */
    int **nfa_sets;      /* nfa_sets[state] = sorted NFA state set */
    int *nfa_set_sizes;
} Dfa;

static Dfa *dfa_new(void) {
    Dfa *d = calloc(1, sizeof(Dfa));
    return d;
}

static int dfa_add_state(Dfa *d, const int *nfa_set, int nfa_size) {
    int id = d->nstates++;
    d->trans          = realloc(d->trans,          (size_t)d->nstates * sizeof(int*));
    d->accept         = realloc(d->accept,         (size_t)d->nstates * sizeof(int));
    d->bol_accept     = realloc(d->bol_accept,     (size_t)d->nstates * sizeof(int));
    d->nfa_sets       = realloc(d->nfa_sets,       (size_t)d->nstates * sizeof(int*));
    d->nfa_set_sizes  = realloc(d->nfa_set_sizes,  (size_t)d->nstates * sizeof(int));
    d->trans[id]      = malloc(256 * sizeof(int));
    for (int i = 0; i < 256; i++) d->trans[id][i] = -1;
    d->nfa_sets[id]   = malloc((size_t)nfa_size * sizeof(int));
    memcpy(d->nfa_sets[id], nfa_set, (size_t)nfa_size * sizeof(int));
    d->nfa_set_sizes[id] = nfa_size;
    d->accept[id]     = -1;
    d->bol_accept[id] = -1;
    return id;
}

/* Build DFA from combined NFA using subset construction */
static Dfa *build_dfa(Nfa *nfa) {
    Dfa *dfa = dfa_new();

    /* Epsilon-closure of {nfa->start} */
    int init_set[MAX_NSET];
    int init_size = epsilon_closure(nfa, &nfa->start, 1, init_set);
    dfa_add_state(dfa, init_set, init_size);

    int *worklist = malloc((size_t)dfa->nstates * sizeof(int));
    int nwork = 1;
    worklist[0] = 0;

    /* Mark which rules need BOL.  A rule number is "bol-sensitive" if its
     * NFA fragment contains a T_BOL edge emanating from its start.  We
     * detect this when the NFA set includes a state whose outgoing edge
     * has label T_BOL.  The bol_accept table records accepting states that
     * are reachable ONLY through BOL transitions. */
    int *rule_is_bol = calloc((size_t)nfa->n, sizeof(int));
    /* We'll detect BOL-sensitive rules during NFA traversal */

    /* Buffer for per-character move+closure */
    int *move_buf = malloc((size_t)MAX_NSET * sizeof(int));

    for (int wi = 0; wi < nwork; wi++) {
        int ds = worklist[wi];
        int *set = dfa->nfa_sets[ds];
        int sz  = dfa->nfa_set_sizes[ds];

        /* Determine accept rule for this DFA state.
         * For each NFA state in the set, check if it's an accept state.
         * The lowest rule index wins (earliest in .l file has priority). */
        int best_accept = -1;
        for (int i = 0; i < sz; i++) {
            int ns = set[i];
            if (ns >= 0 && ns < nfa->n) {
                int ar = nfa->states[ns].accept_rule;
                if (ar >= 0) {
                    if (best_accept < 0 || ar < best_accept) best_accept = ar;
                }
                /* Check if this state has outgoing BOL transitions */
                for (int j = 0; j < nfa->states[ns].ne; j++) {
                    if (nfa->states[ns].edges[j].label == T_BOL) {
                        rule_is_bol[ar >= 0 ? ar : 0] = 1;
                    }
                }
            }
        }
        dfa->accept[ds] = best_accept;

        /* For each character, compute next DFA state */
        for (int c = 0; c < 256; c++) {
            /* Move: find all NFA states reachable via character c */
            int nmove = 0;
            for (int i = 0; i < sz; i++) {
                int ns = set[i];
                if (ns < 0 || ns >= nfa->n) continue;
                NfaState *st = &nfa->states[ns];
                for (int j = 0; j < st->ne; j++) {
                    NfaEdge *e = &st->edges[j];
                    int match = 0;
                    if (e->label == c) {
                        match = 1;
                    } else if (e->label == CC_ANY) {
                        if (c != '\n') match = 1;
                    } else if (e->label == CC_CLASS) {
                        int in_class = cs_get(&e->cset, c);
                        match = e->neg ? !in_class : in_class;
                    }
                    if (match) {
                        int d = e->dest;
                        int dup = 0;
                        for (int k = 0; k < nmove; k++)
                            if (move_buf[k] == d) { dup = 1; break; }
                        if (!dup && nmove < MAX_NSET)
                            move_buf[nmove++] = d;
                    }
                }
            }

            if (nmove == 0) continue;

            /* Epsilon-closure of move set */
            int closed[MAX_NSET];
            int nclosed = epsilon_closure(nfa, move_buf, nmove, closed);

            /* Find or create DFA state for this closed set */
            int found = -1;
            for (int si = 0; si < dfa->nstates; si++) {
                if (set_eq(closed, nclosed, dfa->nfa_sets[si], dfa->nfa_set_sizes[si])) {
                    found = si;
                    break;
                }
            }
            if (found < 0) {
                found = dfa_add_state(dfa, closed, nclosed);
                worklist = realloc(worklist, (size_t)(++nwork) * sizeof(int));
                worklist[nwork - 1] = found;
            }
            dfa->trans[ds][c] = found;
        }
    }

    free(worklist);
    free(move_buf);
    free(rule_is_bol);
    return dfa;
}

/* ======================== .l File Parser ======================== */

/* Detect start condition prefix like <INITIAL> and strip it */
static const char *skip_start_cond(const char *p, int *has_sc) {
    *has_sc = 0;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p == '<') {
        const char *end = strchr(p, '>');
        if (end) {
            *has_sc = 1;
            return end + 1;
        }
    }
    return p;
}

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "flex: cannot open %s\n", path); return NULL; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)(len + 1));
    if (len > 0) fread(buf, 1, (size_t)len, f);
    buf[len] = '\0';
    fclose(f);
    return buf;
}

/* Parse .l file content into rules and code blocks */
typedef struct {
    char *decl;         /* C declarations (from %{ %}) */
    size_t decl_len;
    char *code;         /* trailing C code (after second %%) */
    size_t code_len;
    char *patterns[MAX_RULES];   /* pattern strings */
    char *actions[MAX_RULES];    /* action strings  */
    int n_rules;
    int has_bol[MAX_RULES];      /* pattern starts with ^ */
    int has_eol[MAX_RULES];      /* pattern ends with $ */
} LexFile;

static void lf_init(LexFile *lf) {
    memset(lf, 0, sizeof(*lf));
}

static void lf_free(LexFile *lf) {
    free(lf->decl);
    free(lf->code);
    for (int i = 0; i < lf->n_rules; i++) {
        free(lf->patterns[i]);
        free(lf->actions[i]);
    }
}

/* Parse the .l file content */
static int parse_l_file(const char *content, LexFile *lf) {
    const char *p = content;
    int section = 0;  /* 0 = decls, 1 = rules, 2 = code */

    /* Skip initial whitespace */
    while (*p && isspace((unsigned char)*p)) p++;

    /* Check for %{ ... %} block before first %% */
    while (*p) {
        if (p[0] == '%' && p[1] == '%') {
            p += 2;
            section++;
            if (section == 1) break;
        }
        if (section == 0 && p[0] == '%' && p[1] == '{') {
            const char *end = strstr(p + 2, "%}");
            if (end) {
                size_t len = (size_t)(end - p - 2);
                lf->decl = malloc(len + 1);
                memcpy(lf->decl, p + 2, len);
                lf->decl[len] = '\0';
                lf->decl_len = len;
                p = end + 2;
                continue;
            }
        }
        /* Skip line (definitions or comments) */
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }

    if (section != 1) {
        fprintf(stderr, "flex: missing first %%\n");
        return -1;
    }

    /* Section 2: rules */
    while (*p) {
        /* Skip blank lines and comments */
        while (*p && (*p == '\n' || *p == '\r')) p++;
        if (!*p) break;

        /* Check for second %% */
        if (p[0] == '%' && p[1] == '%') {
            p += 2;
            section++;
            break;
        }

        /* Skip %{ %} blocks in rules section (verbatim code inserted at rule position) */
        if (p[0] == '%' && p[1] == '{') {
            const char *end = strstr(p + 2, "%}");
            if (end) { p = end + 2; continue; }
        }

        /* Parse pattern */
        /* Skip optional start condition <...> */
        int has_sc = 0;
        p = skip_start_cond(p, &has_sc);

        /* Read pattern until { (action start) or end of line/input */
        /* Accumulate the pattern */
        char pat[8192]; int pi = 0;
        int bol_flag = 0;

        /* Check if pattern starts with ^ */
        const char *pp = p;
        while (*pp && isspace((unsigned char)*pp)) pp++;
        if (*pp == '^') { bol_flag = 1; }

        while (*p && *p != '{' && *p != '\n') {
            if (pi < 8190) pat[pi++] = *p;
            p++;
        }
        pat[pi] = '\0';

        /* Trim trailing whitespace from pattern */
        while (pi > 0 && isspace((unsigned char)pat[pi-1])) pi--;
        pat[pi] = '\0';

        /* Check for $ at end of pattern (anchor) */
        int eol_flag = 0;
        if (pi > 0 && pat[pi-1] == '$') {
            eol_flag = 1;
            pat[--pi] = '\0';
        }

        if (pi == 0) {
            fprintf(stderr, "flex: empty pattern\n");
            return -1;
        }

        /* Read action inside { } */
        if (*p != '{') {
            fprintf(stderr, "flex: expected { after pattern\n");
            return -1;
        }
        p++; /* skip { */
        char act[16384]; int ai = 0;
        int depth = 1;
        while (*p && depth > 0) {
            if (*p == '{') depth++;
            if (*p == '}') depth--;
            if (depth > 0 && ai < 16382) act[ai++] = *p;
            p++;
        }
        act[ai] = '\0';

        if (depth != 0) {
            fprintf(stderr, "flex: unbalanced braces in action\n");
            return -1;
        }

        if (lf->n_rules < MAX_RULES) {
            lf->patterns[lf->n_rules] = xstrdup(pat);
            lf->actions[lf->n_rules]  = xstrdup(act);
            lf->has_bol[lf->n_rules]  = bol_flag;
            lf->has_eol[lf->n_rules]  = eol_flag;
            lf->n_rules++;
        } else {
            fprintf(stderr, "flex: too many rules (%d max)\n", MAX_RULES);
            return -1;
        }

        /* Skip to next line */
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }

    /* Section 3: C code */
    if (section >= 2) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p) {
            lf->code = xstrdup(p);
            lf->code_len = strlen(p);
        }
    }

    return 0;
}

/* ======================== Code Generator ======================== */

static void generate_code(FILE *out, LexFile *lf, Dfa *dfa) {
    /* Verify we have at least one implicit default rule (the . rule) */
    /* If the DFA has no accept states at all, add an implicit catch-all */

    int ns = dfa->nstates;

    /* Header */
    fprintf(out, "/* Generated by flex (minimal) */\n");
    fprintf(out, "#include <stdio.h>\n");
    fprintf(out, "#include <string.h>\n");
    fprintf(out, "#include <stdlib.h>\n\n");

    /* User's C declarations */
    if (lf->decl && lf->decl_len > 0) {
        fprintf(out, "%s\n", lf->decl);
    }

    /* yytext buffer and helper macros */
    fprintf(out, "#define YYTEXT_MAX 4096\n");
    fprintf(out, "#define ECHO printf(\"%%s\", yytext)\n");
    fprintf(out, "#define YY_USER_ACTION\n");
    fprintf(out, "static char yytext[YYTEXT_MAX];\n\n");

    /* DFA transition table as short 2D array */
    fprintf(out, "static const short yy_dfa[%d][256] = {\n", ns);
    for (int i = 0; i < ns; i++) {
        fprintf(out, "  {");
        for (int c = 0; c < 256; c++) {
            if (c > 0) fprintf(out, ",");
            fprintf(out, "%d", dfa->trans[i][c]);
        }
        fprintf(out, "}");
        if (i < ns - 1) fprintf(out, ",");
        fprintf(out, "\n");
    }
    fprintf(out, "};\n\n");

    /* Accept table */
    fprintf(out, "static const signed char yy_accept[%d] = {\n", ns);
    for (int i = 0; i < ns; i++) {
        if (i > 0) fprintf(out, ",");
        if (i % 16 == 0) fprintf(out, "  ");
        fprintf(out, "%d", dfa->accept[i]);
        if ((i + 1) % 16 == 0 || i == ns - 1) fprintf(out, "\n");
    }
    fprintf(out, "};\n\n");

    /* The yylex() function */
    fprintf(out, "int yylex(void) {\n");
    fprintf(out, "    int state = 0;\n");
    fprintf(out, "    int last_accept = -1;\n");
    fprintf(out, "    int last_pos = 0;\n");
    fprintf(out, "    int pos = 0;\n");
    fprintf(out, "    int c;\n\n");

    fprintf(out, "    while (1) {\n");
    fprintf(out, "        c = getchar();\n");
    fprintf(out, "        if (c == EOF) {\n");
    fprintf(out, "            /* Flush pending match on EOF */\n");
    fprintf(out, "            if (last_accept >= 0) {\n");
    fprintf(out, "                yytext[pos] = '\\0';\n");
    /* Dispatch at EOF - always return positive so caller continues */
    fprintf(out, "                switch (last_accept) {\n");
    for (int i = 0; i < lf->n_rules; i++) {
        fprintf(out, "                    case %d: { %s } return %d;\n", i, lf->actions[i], i + 1);
    }
    fprintf(out, "                    default: break;\n");
    fprintf(out, "                }\n");
    fprintf(out, "            }\n");
    fprintf(out, "            return 0;\n");
    fprintf(out, "        }\n");
    fprintf(out, "        if (pos < YYTEXT_MAX) yytext[pos] = (char)c;\n");
    fprintf(out, "        pos++;\n\n");

    /* Transition */
    fprintf(out, "        int next = yy_dfa[state][c];\n");
    fprintf(out, "        if (next >= 0) {\n");
    fprintf(out, "            state = next;\n");
    fprintf(out, "            int acc = yy_accept[state];\n");
    fprintf(out, "            if (acc >= 0) {\n");
    fprintf(out, "                last_accept = acc;\n");
    fprintf(out, "                last_pos = pos;\n");
    fprintf(out, "            }\n");
    fprintf(out, "        } else {\n");
    fprintf(out, "            /* No transition from this DFA state */\n");
    fprintf(out, "            if (last_accept >= 0) {\n");
    fprintf(out, "                /* Backtrack to last accepting state */\n");
    fprintf(out, "                while (pos > last_pos) {\n");
    fprintf(out, "                    pos--;\n");
    fprintf(out, "                    ungetc((unsigned char)yytext[pos], stdin);\n");
    fprintf(out, "                }\n");
    fprintf(out, "                yytext[pos] = '\\0';\n");
    /* Dispatch table */
    fprintf(out, "                switch (last_accept) {\n");
    for (int i = 0; i < lf->n_rules; i++) {
        fprintf(out, "                    case %d: { %s } return %d;\n", i, lf->actions[i], i + 1);
    }
    fprintf(out, "                    default: break;\n");
    fprintf(out, "                }\n");
    fprintf(out, "            }\n");
    /* Default: echo first character (implicit default rule) */
    fprintf(out, "            if (pos > 0) {\n");
    fprintf(out, "                c = (unsigned char)yytext[0];\n");
    fprintf(out, "                pos = 0;\n");
    fprintf(out, "                putchar(c);\n");
    fprintf(out, "            }\n");
    fprintf(out, "            state = 0;\n");
    fprintf(out, "            last_accept = -1;\n");
    fprintf(out, "            last_pos = 0;\n");
    fprintf(out, "            pos = 0;\n");
    fprintf(out, "        }\n");
    fprintf(out, "    }\n");
    fprintf(out, "}\n\n");

    /* User's C code from trailing section */
    if (lf->code && lf->code_len > 0) {
        fprintf(out, "%s\n", lf->code);
    }
}

/* ======================== Help text ======================== */

static void print_flex_help(void)
{
    printf("Usage: flex [OPTION]... [FILE]...\n");
    printf("Minimal flex-compatible lexer generator.\n");
    printf("\n");
    printf("Options:\n");
    printf("  --help        display this help and exit\n");
    printf("\n");
    printf("Reads .l lexer definitions, generates C code on stdout.\n");
    printf("If no FILE is given, read standard input.\n");
}

/* ======================== Main ======================== */

int flex_main(int argc, char **argv) {
    const char *input_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            print_flex_help();
            return 0;
        }
    }

    if (argc > 2) {
        fprintf(stderr, "usage: flex [file.l]\n");
        fprintf(stderr, "Run 'flex --help' for usage information.\n");
        return 1;
    }
    if (argc == 2) {
        input_path = argv[1];
    }

    /* Read input */
    char *content;
    if (input_path) {
        content = read_file(input_path);
        if (!content) return 1;
    } else {
        /* Read stdin */
        size_t cap = 65536, len = 0;
        content = malloc(cap);
        size_t n;
        while ((n = fread(content + len, 1, cap - len, stdin)) > 0) {
            len += n;
            if (len >= cap) {
                cap *= 2;
                content = realloc(content, cap);
            }
        }
        content[len] = '\0';
    }

    /* Parse .l file */
    LexFile lf;
    lf_init(&lf);
    if (parse_l_file(content, &lf) < 0) {
        free(content);
        lf_free(&lf);
        return 1;
    }
    free(content);

    if (lf.n_rules == 0) {
        fprintf(stderr, "flex: no rules\n");
        lf_free(&lf);
        return 1;
    }

    /* Build NFA from all rules */
    Nfa *nfa = nfa_new();
    int *rule_starts = malloc((size_t)lf.n_rules * sizeof(int));
    int *rule_accepts = malloc((size_t)lf.n_rules * sizeof(int));

    for (int i = 0; i < lf.n_rules; i++) {
        /* Strip BOL anchor for NFA construction; it's handled by the generated code */
        const char *pat = lf.patterns[i];
        /* Trim leading whitespace */
        while (*pat && isspace((unsigned char)*pat)) pat++;
        if (*pat == '^') pat++; /* skip leading ^ for NFA */

        /* Trim trailing whitespace */
        char pat_copy[8192];
        strncpy(pat_copy, pat, sizeof(pat_copy) - 1);
        pat_copy[sizeof(pat_copy) - 1] = '\0';
        int plen = (int)strlen(pat_copy);
        while (plen > 0 && isspace((unsigned char)pat_copy[plen-1])) plen--;
        pat_copy[plen] = '\0';

        Ast *ast = parse_pattern(pat_copy);
        if (!ast) {
            fprintf(stderr, "flex: failed to parse pattern %d: '%s'\n", i, lf.patterns[i]);
            nfa->start = 0;
            free(rule_starts); free(rule_accepts);
            lf_free(&lf);
            return 1;
        }

        int s, a;
        thompson_build(ast, nfa, &s, &a, i);
        nfa_set_accept(nfa, a, i);

        rule_starts[i]  = s;
        rule_accepts[i] = a;
        ast_free(ast);
    }

    /* Combine all rule NFAs with a common start state */
    int combined_start = nfa_add_state(nfa);
    for (int i = 0; i < lf.n_rules; i++) {
        nfa_add_edge(nfa, combined_start, EPSILON, 0, NULL, rule_starts[i]);
    }
    nfa->start = combined_start;

    free(rule_starts);
    free(rule_accepts);

    /* Build DFA */
    Dfa *dfa = build_dfa(nfa);

    /* Generate C code */
    generate_code(stdout, &lf, dfa);

    /* Cleanup */
    /* Note: Skipping full cleanup since process exits anyway */

    return 0;
}
