/* awk — POSIX awk 子集实现
 * 支持：BEGIN/END、模式-动作、字段($0-$NF)、NR/NF/FILENAME、FS/RS/OFS/ORS、
 *       正则匹配(~ !~)、if/else/while/for/for..in/do、break/continue/next/exit、
 *       print/printf(> >> |)、关联数组、内置函数 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <regex.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "meuos/utils.h"

static void awk_die(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    fprintf(stderr, "awk: "); vfprintf(stderr, fmt, ap); fputc('\n', stderr);
    va_end(ap); exit(2);
}

/* === 动态值 === */
typedef struct { char *s; double n; int is_num; } val_t;
static val_t mk_str(const char *s) { val_t v = {s?strdup(s):strdup(""), 0, 0}; return v; }
static val_t mk_num(double n) { val_t v = {NULL, n, 1}; return v; }
static void v_free(val_t *v) { if (v->s) { free(v->s); v->s = NULL; } }
static const char *v_str(val_t *v) {
    if (v->s) return v->s;
    char buf[64];
    if (v->n == (long long)v->n && v->n < 1e15 && v->n > -1e15)
        snprintf(buf, sizeof(buf), "%.0f", v->n);
    else snprintf(buf, sizeof(buf), "%.6g", v->n);
    v->s = strdup(buf); return v->s;
}
static double v_num(val_t *v) {
    if (v->is_num) return v->n;
    if (v->s && *v->s) return strtod(v->s, NULL);
    return 0;
}

/* === 数组 === */
typedef struct an { char *key; val_t val; struct an *next; } an_t;
typedef struct { an_t *b[64]; } arr_t;
static void arr_init(arr_t *a) { memset(a->b, 0, sizeof(a->b)); }
static unsigned hfn(const char *k) { unsigned h=5381; while(k&&*k) h=h*33+(unsigned char)*k++; return h&63; }
static void arr_set(arr_t *a, const char *k, val_t v) {
    unsigned h=hfn(k); an_t *n=a->b[h];
    while(n){if(strcmp(n->key,k)==0){v_free(&n->val);n->val=v;return;}n=n->next;}
    n=malloc(sizeof(an_t));n->key=strdup(k);n->val=v;n->next=a->b[h];a->b[h]=n;
}
static val_t *arr_get(arr_t *a, const char *k) {
    unsigned h=hfn(k); an_t *n=a->b[h];
    while(n){if(strcmp(n->key,k)==0) return &n->val;n=n->next;}
    return NULL;
}
static void arr_del(arr_t *a, const char *k) {
    unsigned h=hfn(k); an_t **p=&a->b[h];
    while(*p){if(strcmp((*p)->key,k)==0){an_t *t=*p;*p=t->next;v_free(&t->val);free(t->key);free(t);return;}p=&(*p)->next;}
}
static void arr_free(arr_t *a) {
    for(int i=0;i<64;i++){an_t *n=a->b[i];while(n){an_t *t=n->next;v_free(&n->val);free(n->key);free(n);n=t;}}
}

/* === 全局状态 === */
#define MAXFLDS 256
typedef struct {
    char *line, *flds[MAXFLDS]; int nf;
    long nr, f_nr; char *fname;
    char *fs, *rs, *ofs, *ors;
    long rlen, rstart;
    arr_t vars, arrays;
    char **files; int nfiles, cfile;
    FILE *cfp; int eof;
    int rand_seed_init;
} ctx_t;

static val_t *vget(ctx_t *c, const char *name) {
    val_t *v=arr_get(&c->vars,name); if(v) return v; return arr_get(&c->arrays,name);
}
static void vset(ctx_t *c, const char *name, val_t v) { arr_set(&c->vars,name,v); }
static void aset(ctx_t *c, const char *name, const char *key, val_t v) {
    char buf[1024]; snprintf(buf,sizeof(buf),"%s\x01%s",name,key); arr_set(&c->arrays,buf,v);
}
static val_t *aget(ctx_t *c, const char *name, const char *key) {
    char buf[1024]; snprintf(buf,sizeof(buf),"%s\x01%s",name,key); return arr_get(&c->arrays,buf);
}
static void adel(ctx_t *c, const char *name, const char *key) {
    char buf[1024]; snprintf(buf,sizeof(buf),"%s\x01%s",name,key); arr_del(&c->arrays,buf);
}

/* === 词法 === */
typedef enum {
    T_EOF=0,T_NL,T_SEMI,T_NUM,T_STR,T_REGEX,T_IDENT,
    T_PLUS,T_MINUS,T_MUL,T_DIV,T_MOD,T_POW,
    T_ASSIGN,T_PLUSEQ,T_MINUSEQ,T_MULEQ,T_DIVEQ,T_MODEQ,T_POWEQ,
    T_EQ,T_NEQ,T_LT,T_GT,T_LE,T_GE,
    T_NOT,T_AND,T_OR,T_MATCH,T_NMATCH,
    T_DOLLAR,T_INCR,T_DECR,T_QUEST,T_COLON,
    T_LPAREN,T_RPAREN,T_LBRACE,T_RBRACE,T_LBRACKET,T_RBRACKET,
    T_COMMA,T_PIPE,T_GTAM,T_GAPP,
    T_IF,T_ELSE,T_WHILE,T_FOR,T_DO,T_BREAK,T_CONTINUE,T_NEXT,T_EXIT,
    T_PRINT,T_PRINTF,T_IN,T_BEGIN,T_END,T_DELETE,T_RETURN,T_GETLINE,
    T_CALL,
} tt_t;

typedef struct { tt_t t; char *s; double n; int pos; } tok_t;
typedef struct { const char *src; int pos,len; tok_t cur,peek; int hp; int in_p; } lex_t;

static struct { const char *name; tt_t tok; } kw[] = {
    {"if",T_IF},{"else",T_ELSE},{"while",T_WHILE},{"for",T_FOR},
    {"do",T_DO},{"break",T_BREAK},{"continue",T_CONTINUE},
    {"next",T_NEXT},{"exit",T_EXIT},{"print",T_PRINT},{"printf",T_PRINTF},
    {"in",T_IN},{"BEGIN",T_BEGIN},{"END",T_END},
    {"delete",T_DELETE},{"return",T_RETURN},{"getline",T_GETLINE},
    {NULL,0}
};
static tt_t kw_lookup(const char *s) { for(int i=0;kw[i].name;i++) if(strcmp(kw[i].name,s)==0) return kw[i].tok; return T_IDENT; }
static void tok_free(tok_t *t) { if(t->s) { free(t->s); t->s=NULL; } }
static void lex_init(lex_t *l, const char *src) { l->src=src; l->pos=0; l->len=(int)strlen(src); l->hp=0; l->in_p=0; }
static void lex_skipws(lex_t *l) { while(l->pos<l->len && (l->src[l->pos]==' '||l->src[l->pos]=='\t')) l->pos++; }
static void lex_skipcom(lex_t *l) { if(l->pos<l->len && l->src[l->pos]=='#') { while(l->pos<l->len && l->src[l->pos]!='\n') l->pos++; } }

static void read_tok(lex_t *l, tok_t *t) {
    tok_free(t);
    while(1) { lex_skipws(l); if(l->pos<l->len && l->src[l->pos]=='#') lex_skipcom(l); else break; }
    if(l->pos>=l->len) { t->t=T_EOF; t->pos=l->pos; return; }
    int st=l->pos; char c=l->src[l->pos];
    if(c=='\n') { l->pos++; t->t=T_NL; t->pos=st; return; }
    if(c==';') { l->pos++; t->t=T_SEMI; t->pos=st; return; }
    if(isdigit(c)||(c=='.'&&l->pos+1<l->len&&isdigit(l->src[l->pos+1]))) {
        int ds=l->pos;
        while(l->pos<l->len && (isdigit(l->src[l->pos])||l->src[l->pos]=='.'||l->src[l->pos]=='e'||
               l->src[l->pos]=='E'||((l->src[l->pos]=='+'||l->src[l->pos]=='-')&&(l->src[l->pos-1]=='e'||l->src[l->pos-1]=='E'))))
            l->pos++;
        char *b=malloc(l->pos-ds+1); memcpy(b,l->src+ds,l->pos-ds); b[l->pos-ds]='\0';
        t->t=T_NUM; t->n=strtod(b,NULL); t->s=b; t->pos=st; return;
    }
    if(c=='"') {
        l->pos++; int cap=64,ln=0; char *b=malloc(cap);
        while(l->pos<l->len && l->src[l->pos]!='"') {
            char ch=l->src[l->pos++];
            if(ch=='\\' && l->pos<l->len) {
                ch=l->src[l->pos++];
                switch(ch) { case 'n':ch='\n';break; case 't':ch='\t';break; case 'r':ch='\r';break; case '\\':ch='\\';break; case '"':ch='"';break;
                    default: if(ln+2>=cap){cap*=2;b=realloc(b,cap);} b[ln++]='\\'; break; }
            }
            if(ln+1>=cap){cap*=2;b=realloc(b,cap);} b[ln++]=ch;
        }
        b[ln]='\0'; if(l->pos<l->len) l->pos++;
        t->t=T_STR; t->s=b; t->pos=st; return;
    }
    if(c=='/' && !l->in_p) {
        l->pos++; int cap=64,ln=0; char *b=malloc(cap);
        while(l->pos<l->len && l->src[l->pos]!='/') {
            char ch=l->src[l->pos++];
            if(ch=='\\' && l->pos<l->len) { if(ln+2>=cap){cap*=2;b=realloc(b,cap);} b[ln++]=ch; ch=l->src[l->pos++]; }
            if(ln+1>=cap){cap*=2;b=realloc(b,cap);} b[ln++]=ch;
        }
        b[ln]='\0'; if(l->pos<l->len) l->pos++;
        t->t=T_REGEX; t->s=b; t->pos=st; return;
    }
    if(isalpha(c)||c=='_') {
        int ds=l->pos;
        while(l->pos<l->len && (isalnum(l->src[l->pos])||l->src[l->pos]=='_')) l->pos++;
        int ln=l->pos-ds; char *buf=malloc(ln+1); memcpy(buf,l->src+ds,ln); buf[ln]='\0';
        t->t=kw_lookup(buf); t->s=buf; t->pos=st; return;
    }
    if(c=='+'&&l->pos+1<l->len) { if(l->src[l->pos+1]=='+'){l->pos+=2;t->t=T_INCR;t->pos=st;return;}
        if(l->src[l->pos+1]=='='){l->pos+=2;t->t=T_PLUSEQ;t->pos=st;return;} }
    if(c=='-'&&l->pos+1<l->len) { if(l->src[l->pos+1]=='-'){l->pos+=2;t->t=T_DECR;t->pos=st;return;}
        if(l->src[l->pos+1]=='='){l->pos+=2;t->t=T_MINUSEQ;t->pos=st;return;} }
    if(c=='*'&&l->pos+1<l->len&&l->src[l->pos+1]=='='){l->pos+=2;t->t=T_MULEQ;t->pos=st;return;}
    if(c=='/'&&l->pos+1<l->len&&l->src[l->pos+1]=='='){l->pos+=2;t->t=T_DIVEQ;t->pos=st;return;}
    if(c=='%'&&l->pos+1<l->len&&l->src[l->pos+1]=='='){l->pos+=2;t->t=T_MODEQ;t->pos=st;return;}
    if(c=='^'&&l->pos+1<l->len&&l->src[l->pos+1]=='='){l->pos+=2;t->t=T_POWEQ;t->pos=st;return;}
    if(c=='='&&l->pos+1<l->len&&l->src[l->pos+1]=='='){l->pos+=2;t->t=T_EQ;t->pos=st;return;}
    if(c=='!'&&l->pos+1<l->len&&l->src[l->pos+1]=='='){l->pos+=2;t->t=T_NEQ;t->pos=st;return;}
    if(c=='<'&&l->pos+1<l->len&&l->src[l->pos+1]=='='){l->pos+=2;t->t=T_LE;t->pos=st;return;}
    if(c=='>'&&l->pos+1<l->len&&l->src[l->pos+1]=='>'){l->pos+=2;t->t=T_GAPP;t->pos=st;return;}
    if(c=='>'&&l->pos+1<l->len&&l->src[l->pos+1]=='='){l->pos+=2;t->t=T_GE;t->pos=st;return;}
    if(c=='&'&&l->pos+1<l->len&&l->src[l->pos+1]=='&'){l->pos+=2;t->t=T_AND;t->pos=st;return;}
    if(c=='|'&&l->pos+1<l->len&&l->src[l->pos+1]=='|'){l->pos+=2;t->t=T_OR;t->pos=st;return;}
    if(c=='!'&&l->pos+1<l->len&&l->src[l->pos+1]=='~'){l->pos+=2;t->t=T_NMATCH;t->pos=st;return;}
    l->pos++; t->pos=st;
    switch(c) {
        case '+':t->t=T_PLUS;return; case '-':t->t=T_MINUS;return;
        case '*':t->t=T_MUL;return; case '/':t->t=T_DIV;return;
        case '%':t->t=T_MOD;return; case '^':t->t=T_POW;return;
        case '=':t->t=T_ASSIGN;return; case '<':t->t=T_LT;return;
        case '>':t->t=T_GT;return; case '!':t->t=T_NOT;return;
        case '~':t->t=T_MATCH;return; case '$':t->t=T_DOLLAR;return;
        case '?':t->t=T_QUEST;return; case ':':t->t=T_COLON;return;
        case '(':t->t=T_LPAREN;return; case ')':t->t=T_RPAREN;return;
        case '{':t->t=T_LBRACE;return; case '}':t->t=T_RBRACE;return;
        case '[':t->t=T_LBRACKET;return; case ']':t->t=T_RBRACKET;return;
        case ',':t->t=T_COMMA;return; case '|':t->t=T_PIPE;return;
        default: awk_die("unexpected char '%c' at %d",c,st);
    }
}
static tok_t *lex_next(lex_t *l) {
    if(l->hp) { l->hp=0; l->cur=l->peek; l->peek.s=NULL; tok_free(&l->peek); return &l->cur; }
    read_tok(l,&l->cur); return &l->cur;
}
static tok_t *lex_peek(lex_t *l) { if(!l->hp) { read_tok(l,&l->peek); l->hp=1; } return &l->peek; }
static int lex_match(lex_t *l, tt_t t) { if(lex_peek(l)->t==t) { lex_next(l); return 1; } return 0; }

/* === AST === */
typedef enum {
    AST_NUM,AST_STR,AST_REGEX,AST_VAR,AST_FLD,AST_BINOP,AST_UNOP,AST_CONCAT,
    AST_ASSIGN,AST_ASSIGNOP,AST_INCR,AST_CALL,AST_IN,AST_TERN,AST_AREF,
    AST_PRINT,AST_PRINTF,AST_IF,AST_WHILE,AST_DOWHILE,AST_FOR,AST_FORIN,
    AST_BLOCK,AST_BREAK,AST_CONTINUE,AST_NEXT,AST_EXIT,AST_RETURN,
    AST_DELETE,AST_EXPR_STMT,AST_EMPTY,AST_ACTION,AST_BEGIN,AST_END
} at_t;

typedef struct ast {
    at_t t; int line; char *s; double n;
    tt_t op; struct ast *l,*r,*opd,*cond,*ta,*ea;
    struct ast **ch; int nc,cap;
    char *redir, *in_var, *in_arr;
} ast_t;

static ast_t *an(at_t t) { ast_t *a=calloc(1,sizeof(ast_t)); a->t=t; return a; }
static ast_t *an_num(double n) { ast_t *a=an(AST_NUM); a->n=n; return a; }
static ast_t *an_str(const char *s) { ast_t *a=an(AST_STR); a->s=strdup(s); return a; }
static void ach(ast_t *p, ast_t *c) {
    if(p->nc>=p->cap) { p->cap=p->cap?p->cap*2:4; p->ch=realloc(p->ch,sizeof(ast_t*)*p->cap); }
    p->ch[p->nc++]=c;
}
static void afree(ast_t *a) {
    if(!a) return;
    afree(a->l); afree(a->r); afree(a->opd); afree(a->cond); afree(a->ta); afree(a->ea);
    if(a->ch) { for(int i=0;i<a->nc;i++) afree(a->ch[i]); free(a->ch); }
    if(a->s) free(a->s); if(a->redir) free(a->redir);
    if(a->in_var) free(a->in_var); if(a->in_arr) free(a->in_arr);
    free(a);
}

/* === 语法分析 === */
typedef struct { lex_t *l; ast_t *begin, *end, *body; int pdepth; } pctx_t;
static ast_t *parse_expr(pctx_t *p);

static ast_t *parse_primary(pctx_t *p) {
    lex_t *l=p->l; tok_t *t=lex_peek(l);
    if(t->t==T_NUM) { lex_next(l); return an_num(t->n); }
    if(t->t==T_STR) { char *s=strdup(t->s); lex_next(l); return an_str(s); }
    if(t->t==T_REGEX) { char *s=strdup(t->s); lex_next(l); ast_t *a=an(AST_REGEX); a->s=s; return a; }
    if(t->t==T_LPAREN) { lex_next(l); p->pdepth++; ast_t *e=parse_expr(p); p->pdepth--; if(lex_peek(l)->t==T_RPAREN) lex_next(l); return e; }
    if(t->t==T_DOLLAR) { lex_next(l); ast_t *e=parse_primary(p); ast_t *f=an(AST_FLD); f->opd=e; return f; }
    if(t->t==T_IDENT) {
        char *id=strdup(t->s); lex_next(l);
        if(lex_peek(l)->t==T_LPAREN) {
            ast_t *call=an(AST_CALL); call->s=id;
            lex_next(l);
            if(lex_peek(l)->t!=T_RPAREN) { ach(call,parse_expr(p)); while(lex_match(l,T_COMMA)) ach(call,parse_expr(p)); }
            if(lex_peek(l)->t==T_RPAREN) lex_next(l);
            return call;
        }
        if(lex_peek(l)->t==T_IN) { lex_next(l); ast_t *arr=parse_expr(p); ast_t *in=an(AST_IN); in->s=id; in->r=arr; return in; }
        if(lex_peek(l)->t==T_LBRACKET) {
            lex_next(l); ast_t *idx=parse_expr(p); ast_t *arr=an(AST_AREF);
            arr->s=id; arr->ch=malloc(sizeof(ast_t*)); arr->ch[0]=idx; arr->nc=1; arr->cap=1;
            while(lex_match(l,T_COMMA)) ach(arr,parse_expr(p));
            if(lex_peek(l)->t==T_RBRACKET) lex_next(l);
            return arr;
        }
        ast_t *v=an(AST_VAR); v->s=id; return v;
    }
    if(t->t==T_NOT) { lex_next(l); ast_t *e=parse_primary(p); ast_t *u=an(AST_UNOP); u->op=T_NOT; u->opd=e; return u; }
    if(t->t==T_MINUS) { lex_next(l); ast_t *e=parse_primary(p); ast_t *u=an(AST_UNOP); u->op=T_MINUS; u->opd=e; return u; }
    if(t->t==T_INCR||t->t==T_DECR) { lex_next(l); ast_t *e=parse_primary(p); ast_t *u=an(AST_INCR); u->op=t->t; u->opd=e; return u; }
    if(t->t==T_GETLINE) {
        lex_next(l); ast_t *c=an(AST_CALL); c->s=strdup("getline");
        tok_t *nt=lex_peek(l);
        if(nt->t==T_IDENT||nt->t==T_GETLINE||nt->t==T_DOLLAR||nt->t==T_STR) ach(c,parse_expr(p));
        return c;
    }
    awk_die("unexpected token at pos %d", t->pos);
    return an_num(0);
}
static ast_t *parse_postfix(pctx_t *p) {
    lex_t *l=p->l; ast_t *e=parse_primary(p);
    while(lex_peek(l)->t==T_INCR||lex_peek(l)->t==T_DECR) { tok_t *t=lex_next(l); ast_t *u=an(AST_INCR); u->op=t->t; u->opd=e; e=u; }
    return e;
}
static ast_t *parse_mul(pctx_t *p) {
    ast_t *e=parse_postfix(p); lex_t *l=p->l;
    while(lex_peek(l)->t==T_MUL||lex_peek(l)->t==T_DIV||lex_peek(l)->t==T_MOD) {
        tt_t op=lex_peek(l)->t; lex_next(l); ast_t *r=parse_postfix(p);
        ast_t *b=an(AST_BINOP); b->op=op; b->l=e; b->r=r; e=b;
    }
    return e;
}
static ast_t *parse_concat(pctx_t *p) {
    ast_t *e=parse_mul(p); lex_t *l=p->l;
    while(lex_peek(l)->t==T_STR||lex_peek(l)->t==T_NUM||lex_peek(l)->t==T_DOLLAR||
          lex_peek(l)->t==T_IDENT||lex_peek(l)->t==T_LPAREN||lex_peek(l)->t==T_REGEX||
          lex_peek(l)->t==T_NOT) {
        ast_t *r=parse_mul(p);
        ast_t *b=an(AST_CONCAT); b->l=e; b->r=r; e=b;
    }
    return e;
}
static ast_t *parse_add(pctx_t *p) {
    ast_t *e=parse_concat(p); lex_t *l=p->l;
    while(lex_peek(l)->t==T_PLUS||lex_peek(l)->t==T_MINUS) {
        tt_t op=lex_peek(l)->t; lex_next(l); ast_t *r=parse_concat(p);
        ast_t *b=an(AST_BINOP); b->op=op; b->l=e; b->r=r; e=b;
    }
    return e;
}
static ast_t *parse_cmp(pctx_t *p) {
    ast_t *e=parse_add(p); lex_t *l=p->l;
    while(lex_peek(l)->t==T_LT||lex_peek(l)->t==T_LE||lex_peek(l)->t==T_GE||lex_peek(l)->t==T_EQ||lex_peek(l)->t==T_NEQ||
          (lex_peek(l)->t==T_GT&&!(l->in_p&&p->pdepth==0))) {
        tt_t op=lex_peek(l)->t; lex_next(l); ast_t *r=parse_add(p);
        ast_t *b=an(AST_BINOP); b->op=op; b->l=e; b->r=r; e=b;
    }
    return e;
}
static ast_t *parse_match(pctx_t *p) {
    ast_t *e=parse_cmp(p); lex_t *l=p->l;
    while(lex_peek(l)->t==T_MATCH||lex_peek(l)->t==T_NMATCH) {
        tt_t op=lex_peek(l)->t; lex_next(l); ast_t *r=parse_cmp(p);
        ast_t *b=an(AST_BINOP); b->op=op; b->l=e; b->r=r; e=b;
    }
    return e;
}
static ast_t *parse_not(pctx_t *p) {
    if(lex_peek(p->l)->t==T_NOT) { lex_next(p->l); ast_t *e=parse_not(p); ast_t *u=an(AST_UNOP); u->op=T_NOT; u->opd=e; return u; }
    return parse_match(p);
}
static ast_t *parse_and(pctx_t *p) {
    ast_t *e=parse_not(p); lex_t *l=p->l;
    while(lex_peek(l)->t==T_AND) { lex_next(l); ast_t *r=parse_not(p); ast_t *b=an(AST_BINOP); b->op=T_AND; b->l=e; b->r=r; e=b; }
    return e;
}
static ast_t *parse_or(pctx_t *p) {
    ast_t *e=parse_and(p); lex_t *l=p->l;
    while(lex_peek(l)->t==T_OR) { lex_next(l); ast_t *r=parse_and(p); ast_t *b=an(AST_BINOP); b->op=T_OR; b->l=e; b->r=r; e=b; }
    return e;
}
static ast_t *parse_tern(pctx_t *p) {
    ast_t *c=parse_or(p);
    if(lex_peek(p->l)->t==T_QUEST) { lex_next(p->l); ast_t *a=parse_expr(p); if(lex_peek(p->l)->t==T_COLON) lex_next(p->l); ast_t *b=parse_expr(p); ast_t *t=an(AST_TERN); t->cond=c; t->ta=a; t->ea=b; return t; }
    return c;
}
static ast_t *parse_assign(pctx_t *p) {
    lex_t *l=p->l; ast_t *e=parse_tern(p);
    if(lex_peek(l)->t==T_ASSIGN) { lex_next(l); ast_t *r=parse_expr(p); ast_t *a=an(AST_ASSIGN); a->l=e; a->r=r; return a; }
    if(lex_peek(l)->t>=T_PLUSEQ&&lex_peek(l)->t<=T_POWEQ) { tt_t op=lex_next(l)->t; ast_t *r=parse_expr(p); ast_t *a=an(AST_ASSIGNOP); a->op=op; a->l=e; a->r=r; return a; }
    return e;
}
static ast_t *parse_expr(pctx_t *p) { return parse_assign(p); }
static ast_t *parse_stmts(pctx_t *p);

static ast_t *parse_block(pctx_t *p) {
    lex_t *l=p->l; ast_t *blk=an(AST_BLOCK);
    while(lex_peek(l)->t!=T_RBRACE&&lex_peek(l)->t!=T_EOF) {
        ast_t *s=parse_stmts(p);
        if(s->t!=AST_EMPTY) ach(blk,s);
    }
    if(lex_peek(l)->t==T_RBRACE) lex_next(l);
    return blk;
}
static ast_t *parse_one_stmt(pctx_t *p) {
    lex_t *l=p->l;
    while(lex_peek(l)->t==T_NL) lex_next(l);
    tok_t *t=lex_peek(l);
    if(t->t==T_RBRACE||t->t==T_EOF) return an(AST_EMPTY);
    if(t->t==T_LBRACE) { lex_next(l); return parse_block(p); }
    if(t->t==T_IF) {
        lex_next(l); if(!lex_match(l,T_LPAREN))awk_die("expect '(' after if");
        ast_t *c=parse_expr(p); if(!lex_match(l,T_RPAREN))awk_die("expect ')'");
        ast_t *tb=parse_one_stmt(p); ast_t *eb=NULL;
        if(lex_peek(l)->t==T_ELSE){lex_next(l);eb=parse_one_stmt(p);}
        ast_t *iff=an(AST_IF); iff->cond=c; iff->ta=tb; iff->ea=eb; return iff;
    }
    if(t->t==T_WHILE) {
        lex_next(l); if(!lex_match(l,T_LPAREN))awk_die("expect '(' after while");
        ast_t *c=parse_expr(p); if(!lex_match(l,T_RPAREN))awk_die("expect ')'");
        ast_t *b=parse_one_stmt(p); ast_t *w=an(AST_WHILE); w->cond=c; w->opd=b; return w;
    }
    if(t->t==T_DO) {
        lex_next(l); ast_t *b=parse_one_stmt(p);
        if(lex_peek(l)->t!=T_WHILE)awk_die("expect 'while' in do");
        lex_next(l); if(!lex_match(l,T_LPAREN))awk_die("expect '('");
        ast_t *c=parse_expr(p); if(!lex_match(l,T_RPAREN))awk_die("expect ')'");
        lex_match(l,T_NL); ast_t *d=an(AST_DOWHILE); d->cond=c; d->opd=b; return d;
    }
    if(t->t==T_FOR) {
        lex_next(l); if(!lex_match(l,T_LPAREN))awk_die("expect '(' after for");
        /* Check for for..in: T_IDENT T_IN */
        {
            int save_pos=l->pos;
            if(lex_peek(l)->t==T_IDENT) {
                tok_t *id=lex_next(l);
                char *id_str=strdup(id->s?id->s:"");
                if(lex_peek(l)->t==T_IN) {
                    lex_next(l); ast_t *arr=parse_expr(p);
                    if(!lex_match(l,T_RPAREN))awk_die("expect ')'");
                    ast_t *b=parse_one_stmt(p);
                    ast_t *fi=an(AST_FORIN); fi->in_var=id_str;
                    fi->in_arr=strdup(arr->s?arr->s:""); fi->opd=b; return fi;
                }
                free(id_str);
                /* Not for..in: restore lexer position for C-style for */
                tok_free(&l->peek); l->hp=0; l->pos=save_pos;
            }
        }
        ast_t *init=NULL;
        if(lex_peek(l)->t!=T_SEMI) init=parse_expr(p);
        if(!lex_match(l,T_SEMI))awk_die("expect ';'");
        ast_t *cond=NULL;
        if(lex_peek(l)->t!=T_SEMI) cond=parse_expr(p);
        if(!lex_match(l,T_SEMI))awk_die("expect ';'");
        ast_t *step=NULL;
        if(lex_peek(l)->t!=T_RPAREN) step=parse_expr(p);
        if(!lex_match(l,T_RPAREN))awk_die("expect ')'");
        ast_t *b=parse_one_stmt(p);
        ast_t *f=an(AST_FOR); f->cond=cond;
        f->ch=calloc(3,sizeof(ast_t*)); f->ch[0]=init; f->ch[1]=step; f->ch[2]=b; f->nc=3; f->cap=3;
        return f;
    }
    if(t->t==T_BREAK) { lex_next(l); return an(AST_BREAK); }
    if(t->t==T_CONTINUE) { lex_next(l); return an(AST_CONTINUE); }
    if(t->t==T_NEXT) { lex_next(l); return an(AST_NEXT); }
    if(t->t==T_EXIT) { lex_next(l); ast_t *e=an(AST_EXIT); if(lex_peek(l)->t!=T_NL&&lex_peek(l)->t!=T_SEMI&&lex_peek(l)->t!=T_RBRACE) e->opd=parse_expr(p); return e; }
    if(t->t==T_RETURN) { lex_next(l); ast_t *e=an(AST_RETURN); if(lex_peek(l)->t!=T_NL&&lex_peek(l)->t!=T_SEMI&&lex_peek(l)->t!=T_RBRACE) e->opd=parse_expr(p); return e; }
    if(t->t==T_DELETE) { lex_next(l); ast_t *a=parse_expr(p); ast_t *d=an(AST_DELETE); d->opd=a; return d; }
    if(t->t==T_PRINT||t->t==T_PRINTF) {
        int is_pf=(t->t==T_PRINTF); l->in_p=1; lex_next(l);
        ast_t *pl=an(is_pf?AST_PRINTF:AST_PRINT);
        /* Check for redirection immediately after print (no expressions) */
        tok_t *nt=lex_peek(l);
        if(nt->t==T_GT) { pl->redir=strdup(">"); lex_next(l); pl->opd=parse_expr(p); }
        else if(nt->t==T_GAPP) { pl->redir=strdup(">>"); lex_next(l); pl->opd=parse_expr(p); }
        else if(nt->t==T_PIPE) { pl->redir=strdup("|"); lex_next(l); pl->opd=parse_expr(p); }
        else if(lex_peek(l)->t!=T_NL&&lex_peek(l)->t!=T_SEMI&&lex_peek(l)->t!=T_RBRACE&&lex_peek(l)->t!=T_EOF) {
            /* Parse expression list, checking for redirection after each expression */
            ach(pl,parse_expr(p));
            /* After an expression, check if next token is redirection operator */
            while(lex_peek(l)->t==T_GT||lex_peek(l)->t==T_GAPP||lex_peek(l)->t==T_PIPE) {
                tok_t *rd=lex_next(l);
                if(rd->t==T_GT) pl->redir=strdup(">");
                else if(rd->t==T_GAPP) pl->redir=strdup(">>");
                else pl->redir=strdup("|");
                pl->opd=parse_expr(p);
                break; /* only one redirection allowed */
            }
            /* Continue parsing remaining expressions separated by comma */
            while(lex_match(l,T_COMMA)) {
                if(lex_peek(l)->t==T_NL||lex_peek(l)->t==T_SEMI||lex_peek(l)->t==T_RBRACE) break;
                ach(pl,parse_expr(p));
                /* Check for redirection after this expression too */
                while(lex_peek(l)->t==T_GT||lex_peek(l)->t==T_GAPP||lex_peek(l)->t==T_PIPE) {
                    tok_t *rd=lex_next(l);
                    if(rd->t==T_GT) pl->redir=strdup(">");
                    else if(rd->t==T_GAPP) pl->redir=strdup(">>");
                    else pl->redir=strdup("|");
                    pl->opd=parse_expr(p);
                    break;
                }
            }
        }
        l->in_p=0; return pl;
    }
    return parse_expr(p);
}

static ast_t *parse_stmts(pctx_t *p) {
    lex_t *l=p->l; ast_t *blk=an(AST_BLOCK);
    while(1) {
        while(lex_peek(l)->t==T_NL) lex_next(l);
        if(lex_peek(l)->t==T_EOF||lex_peek(l)->t==T_RBRACE) break;
        ast_t *s=parse_one_stmt(p);
        if(s->t!=AST_EMPTY) ach(blk,s);
        if(lex_peek(l)->t==T_SEMI) { lex_next(l); continue; }
        if(lex_peek(l)->t==T_NL) { lex_next(l); continue; }
        break;
    }
    return blk;
}

static void parse_program(pctx_t *p) {
    lex_t *l=p->l;
    p->begin=NULL; p->end=NULL; p->body=an(AST_BLOCK);
    while(lex_peek(l)->t!=T_EOF) {
        while(lex_peek(l)->t==T_NL) lex_next(l);
        if(lex_peek(l)->t==T_EOF) break;
        if(lex_peek(l)->t==T_BEGIN) { lex_next(l); if(lex_peek(l)->t!=T_LBRACE)awk_die("expect '{' after BEGIN"); lex_next(l); p->begin=parse_stmts(p); if(lex_peek(l)->t==T_RBRACE) lex_next(l); continue; }
        if(lex_peek(l)->t==T_END) { lex_next(l); if(lex_peek(l)->t!=T_LBRACE)awk_die("expect '{' after END"); lex_next(l); p->end=parse_stmts(p); if(lex_peek(l)->t==T_RBRACE) lex_next(l); continue; }
        if(lex_peek(l)->t==T_LBRACE) { lex_next(l); ast_t *blk=parse_stmts(p); if(lex_peek(l)->t==T_RBRACE) lex_next(l); ast_t *a=an(AST_ACTION); a->opd=blk; ach(p->body,a); continue; }
        tok_t *t=lex_peek(l); ast_t *pat=NULL;
        if(t->t==T_REGEX) { char *s=strdup(t->s); lex_next(l); pat=an(AST_REGEX); pat->s=s; }
        else pat=parse_expr(p);
        if(pat && lex_peek(l)->t==T_COMMA) {
            lex_next(l); ast_t *pat2=NULL;
            if(lex_peek(l)->t==T_REGEX) { tok_t *r=lex_next(l); pat2=an(AST_REGEX); pat2->s=r->s; } else pat2=parse_expr(p);
            ast_t *a=an(AST_ACTION); a->cond=pat; a->ta=pat2;
            while(lex_peek(l)->t==T_NL) lex_next(l);
            if(lex_peek(l)->t==T_LBRACE) { lex_next(l); a->opd=parse_stmts(p); if(lex_peek(l)->t==T_RBRACE) lex_next(l); }
            ach(p->body,a); continue;
        }
        ast_t *a=an(AST_ACTION); a->cond=pat;
        while(lex_peek(l)->t==T_NL) lex_next(l);
        if(lex_peek(l)->t==T_LBRACE) { lex_next(l); a->opd=parse_stmts(p); if(lex_peek(l)->t==T_RBRACE) lex_next(l); }
        else if(lex_peek(l)->t!=T_NL&&lex_peek(l)->t!=T_EOF&&lex_peek(l)->t!=T_RBRACE) a->opd=parse_one_stmt(p);
        ach(p->body,a);
    }
}

/* === 执行引擎 === */
typedef enum { JT_NONE, JT_BREAK, JT_CONTINUE, JT_NEXT, JT_EXIT, JT_RETURN } jt_t;

static char *field_str(ctx_t *c, int idx) {
    if(idx==0) return c->line?c->line:"";
    if(idx>0&&idx<=c->nf) return c->flds[idx-1]?c->flds[idx-1]:"";
    return "";
}
static int ere_match(const char *pat, const char *str, regmatch_t *m, int nmatch) {
    regex_t re; int rc=regcomp(&re,pat,REG_EXTENDED);
    if(rc!=0){char eb[256];regerror(rc,&re,eb,sizeof(eb));fprintf(stderr,"awk: regex error: %s\n",eb);return 0;}
    rc=regexec(&re,str,nmatch,m,0); regfree(&re); return rc==0;
}

static void split_fields(ctx_t *c);
static val_t eval_expr(pctx_t *p, ast_t *a, ctx_t *c) {
    if(!a) return mk_num(0);
    regmatch_t m[10];
    switch(a->t) {
    case AST_NUM: return mk_num(a->n);
    case AST_STR: return mk_str(a->s);
    case AST_REGEX:
        if(ere_match(a->s,c->line?c->line:"",m,1)){c->rstart=m[0].rm_so+1;c->rlen=m[0].rm_eo-m[0].rm_so;return mk_num(1);}
        c->rstart=0;c->rlen=-1;return mk_num(0);
    case AST_VAR: { val_t *v=vget(c,a->s); if(v) return *v; return mk_num(0); }
    case AST_FLD: { val_t iv=eval_expr(p,a->opd,c); return mk_str(field_str(c,(int)v_num(&iv))); }
    case AST_IN: { val_t *arr=aget(c,a->r->s?a->r->s:"",a->s); return mk_num(arr?1:0); }
    case AST_AREF: {
        char key[1024]=""; for(int i=0;i<a->nc;i++){val_t kv=eval_expr(p,a->ch[i],c);if(i>0)strcat(key,"\x01");strcat(key,v_str(&kv));}
        val_t *v=aget(c,a->s,key); if(v) return *v; return mk_num(0);
    }
    case AST_BINOP: {
        if(a->op==T_AND||a->op==T_OR) {
            val_t lv=eval_expr(p,a->l,c); double ln=v_num(&lv);
            if(a->op==T_AND){if(!ln)return mk_num(0);val_t rv=eval_expr(p,a->r,c);return mk_num(v_num(&rv)!=0);}
            else{if(ln)return mk_num(1);val_t rv=eval_expr(p,a->r,c);return mk_num(v_num(&rv)!=0);}
        }
        if(a->op==T_MATCH) {
            val_t lv=eval_expr(p,a->l,c);
            if(a->r->t==AST_REGEX && ere_match(a->r->s,v_str(&lv),m,1)){c->rstart=m[0].rm_so+1;c->rlen=m[0].rm_eo-m[0].rm_so;return mk_num(1);}
            c->rstart=0;c->rlen=-1;return mk_num(0);
        }
        if(a->op==T_NMATCH) {
            val_t lv=eval_expr(p,a->l,c);
            if(a->r->t==AST_REGEX && !ere_match(a->r->s,v_str(&lv),m,1)) return mk_num(1);
            return mk_num(0);
        }
        { val_t lv=eval_expr(p,a->l,c); val_t rv=eval_expr(p,a->r,c);
        double ln=v_num(&lv),rn=v_num(&rv);
        switch(a->op) {
            case T_PLUS:return mk_num(ln+rn);case T_MINUS:return mk_num(ln-rn);
            case T_MUL:return mk_num(ln*rn);case T_DIV:return mk_num(rn!=0?ln/rn:0);
            case T_MOD:return mk_num(rn!=0?(long long)ln%(long long)rn:0);
            case T_POW:return mk_num(pow(ln,rn));
            case T_LT:return mk_num(ln<rn);case T_GT:return mk_num(ln>rn);
            case T_LE:return mk_num(ln<=rn);case T_GE:return mk_num(ln>=rn);
            case T_EQ:return mk_num(ln==rn);case T_NEQ:return mk_num(ln!=rn);
            default:return mk_num(0);} }
    }
    case AST_CONCAT: {
        val_t lv=eval_expr(p,a->l,c); val_t rv=eval_expr(p,a->r,c);
        const char *ls=v_str(&lv),*rs=v_str(&rv);
        int ll=strlen(ls),rl=strlen(rs);
        char *buf=malloc(ll+rl+1);
        memcpy(buf,ls,ll);memcpy(buf+ll,rs,rl);buf[ll+rl]='\0';
        val_t res=mk_str(buf);free(buf);return res;
    }
    case AST_UNOP: { val_t v=eval_expr(p,a->opd,c); if(a->op==T_NOT)return mk_num(v_num(&v)==0); if(a->op==T_MINUS)return mk_num(-v_num(&v)); return v; }
    case AST_INCR: {
        if(a->opd->t==AST_VAR) {
            val_t *v=vget(c,a->opd->s); double old=v?v_num(v):0; double nv=(a->op==T_INCR)?old+1:old-1;
            vset(c,a->opd->s,mk_num(nv)); return mk_num(nv);
        }
        if(a->opd->t==AST_FLD) {
            val_t iv=eval_expr(p,a->opd->opd,c); int idx=(int)v_num(&iv);
            double old=strtod(field_str(c,idx),NULL); double nv=(a->op==T_INCR)?old+1:old-1;
            if(idx==0){char b[4096];snprintf(b,sizeof(b),"%g",nv);free(c->line);c->line=strdup(b);}
            else if(idx>0&&idx<=c->nf){char b[1024];snprintf(b,sizeof(b),"%g",nv);free(c->flds[idx-1]);c->flds[idx-1]=strdup(b);}
            return mk_num(nv);
        }
        if(a->opd->t==AST_AREF) {
            char key[1024]=""; for(int i=0;i<a->opd->nc;i++){val_t kv=eval_expr(p,a->opd->ch[i],c);if(i>0)strcat(key,"\x01");strcat(key,v_str(&kv));}
            val_t *v=aget(c,a->opd->s,key); double old=v?v_num(v):0; double nv=(a->op==T_INCR)?old+1:old-1;
            aset(c,a->opd->s,key,mk_num(nv)); return mk_num(nv);
        }
        return mk_num(0);
    }
    case AST_ASSIGN: {
        val_t v=eval_expr(p,a->r,c);
        if(a->l->t==AST_VAR){vset(c,a->l->s,v);return v;}
        if(a->l->t==AST_FLD) {
            val_t iv=eval_expr(p,a->l->opd,c); int idx=(int)v_num(&iv); char *s=strdup(v_str(&v));
            if(idx==0){free(c->line);c->line=s;} else if(idx>0&&idx<=c->nf){free(c->flds[idx-1]);c->flds[idx-1]=s;} else free(s);
            return v;
        }
        if(a->l->t==AST_AREF) {
            char key[1024]=""; for(int i=0;i<a->l->nc;i++){val_t kv=eval_expr(p,a->l->ch[i],c);if(i>0)strcat(key,"\x01");strcat(key,v_str(&kv));}
            aset(c,a->l->s,key,v); return v;
        }
        return v;
    }
    case AST_ASSIGNOP: {
        double old=0;
        if(a->l->t==AST_VAR){val_t *v=vget(c,a->l->s);old=v?v_num(v):0;}
        if(a->l->t==AST_FLD){val_t iv=eval_expr(p,a->l->opd,c);int idx=(int)v_num(&iv);old=strtod(field_str(c,idx),NULL);}
        if(a->l->t==AST_AREF){char key[1024]="";for(int i=0;i<a->l->nc;i++){val_t kv=eval_expr(p,a->l->ch[i],c);if(i>0)strcat(key,"\x01");strcat(key,v_str(&kv));}val_t *v=aget(c,a->l->s,key);old=v?v_num(v):0;}
        val_t rv=eval_expr(p,a->r,c); double rn=v_num(&rv),nv=0;
        switch(a->op){case T_PLUSEQ:nv=old+rn;break;case T_MINUSEQ:nv=old-rn;break;case T_MULEQ:nv=old*rn;break;
            case T_DIVEQ:nv=rn!=0?old/rn:0;break;case T_MODEQ:nv=rn!=0?(long long)old%(long long)rn:0;break;
            case T_POWEQ:nv=pow(old,rn);break;default:nv=old;}
        val_t newv=mk_num(nv);
        if(a->l->t==AST_VAR)vset(c,a->l->s,newv);
        if(a->l->t==AST_FLD){val_t __iv=eval_expr(p,a->l->opd,c);int idx=(int)v_num(&__iv);char *s=strdup(v_str(&newv));if(idx==0){free(c->line);c->line=s;}else if(idx>0&&idx<=c->nf){free(c->flds[idx-1]);c->flds[idx-1]=s;}else free(s);}
        if(a->l->t==AST_AREF){char key[1024]="";for(int i=0;i<a->l->nc;i++){val_t kv=eval_expr(p,a->l->ch[i],c);if(i>0)strcat(key,"\x01");strcat(key,v_str(&kv));}aset(c,a->l->s,key,newv);}
        return newv;
    }
    case AST_TERN: { val_t cv=eval_expr(p,a->cond,c); if(v_num(&cv)!=0) return eval_expr(p,a->ta,c); return eval_expr(p,a->ea,c); }
    case AST_CALL: {
        const char *fn=a->s;
        if(strcmp(fn,"length")==0){if(a->nc==0)return mk_num(strlen(c->line?c->line:""));val_t v=eval_expr(p,a->ch[0],c);return mk_num(strlen(v_str(&v)));}
        if(strcmp(fn,"substr")==0){val_t sv=eval_expr(p,a->ch[0],c);const char *s=v_str(&sv);
            val_t __sv=eval_expr(p,a->ch[1],c);int start=(int)v_num(&__sv)-1;int len=-1;
            if(a->nc>2){val_t __lv=eval_expr(p,a->ch[2],c);len=(int)v_num(&__lv);}
            if(start<0)start=0;int sl=(int)strlen(s);if(start>=sl)return mk_str("");
            if(len<0||start+len>sl)len=sl-start;char *r=malloc(len+1);memcpy(r,s+start,len);r[len]='\0';val_t rv=mk_str(r);free(r);return rv;}
        if(strcmp(fn,"index")==0){val_t sv=eval_expr(p,a->ch[0],c);val_t sub=eval_expr(p,a->ch[1],c);const char *ps=strstr(v_str(&sv),v_str(&sub));return mk_num(ps?(int)(ps-v_str(&sv))+1:0);}
        if(strcmp(fn,"split")==0){val_t sv=eval_expr(p,a->ch[0],c);const char *arr=a->ch[1]->s;const char *fs=" ";
            if(a->nc>2){val_t __fv=eval_expr(p,a->ch[2],c);fs=v_str(&__fv);}
            char *cpy=strdup(v_str(&sv));int cnt=0;char *tok=strtok(cpy,fs);
            while(tok){char k[64];snprintf(k,sizeof(k),"%d",cnt+1);aset(c,arr,k,mk_str(tok));cnt++;tok=strtok(NULL,fs);}
            free(cpy);return mk_num(cnt);}
        if(strcmp(fn,"sprintf")==0){val_t fv=eval_expr(p,a->ch[0],c);const char *fmt=v_str(&fv);
            char buf[4096];int bi=0,ai=1;
            for(int i=0;fmt[i]&&bi<4090;i++){if(fmt[i]=='%'&&fmt[i+1]){
                i++;if(fmt[i]=='s'){if(ai<a->nc){val_t av=eval_expr(p,a->ch[ai],c);const char *s=v_str(&av);int sl=(int)strlen(s);memcpy(buf+bi,s,sl<4090-bi?sl:4090-bi);bi+=sl;}ai++;}
                else if(fmt[i]=='d'){if(ai<a->nc){val_t av=eval_expr(p,a->ch[ai],c);bi+=sprintf(buf+bi,"%d",(int)v_num(&av));}ai++;}
                else if(fmt[i]=='g'||fmt[i]=='f'){if(ai<a->nc){val_t av=eval_expr(p,a->ch[ai],c);bi+=sprintf(buf+bi,"%g",v_num(&av));}ai++;}
                else{buf[bi++]='%';buf[bi++]=fmt[i];}}else buf[bi++]=fmt[i];}
            buf[bi]='\0';return mk_str(buf);}
        if(strcmp(fn,"sub")==0||strcmp(fn,"gsub")==0){int g=(strcmp(fn,"gsub")==0);
                        val_t rv=eval_expr(p,a->ch[1],c);
                        const char *pat;char *pat_alloc=NULL;
                        if(a->ch[0]->t==AST_REGEX){pat=a->ch[0]->s;}
                        else{val_t pv=eval_expr(p,a->ch[0],c);pat=v_str(&pv);}
                        const char *rep=v_str(&rv);
                        char *tgt;int tgt_is_field=0;int tgt_idx=0;
                        if(a->nc>=3){val_t tv=eval_expr(p,a->ch[2],c);tgt=strdup(v_str(&tv));if(a->ch[2]->t==AST_FLD){tgt_is_field=1;val_t iv=eval_expr(p,a->ch[2]->opd,c);tgt_idx=(int)v_num(&iv);}}
                        else{tgt=strdup(c->line?c->line:"");tgt_is_field=1;tgt_idx=0;}
                        int count=0;
                        char *result=malloc(strlen(tgt)+1);result[0]='\0';int rl=0;int rcap=strlen(tgt)+1;
                        regmatch_t mm;char *pos=tgt;
            while(ere_match(pat,pos,&mm,1)){int pl=mm.rm_so;
                if(rl+pl+strlen(rep)+1>=rcap){rcap=(rl+pl+strlen(rep)+1)*2;result=realloc(result,rcap);}
                memcpy(result+rl,pos,pl);rl+=pl;memcpy(result+rl,rep,strlen(rep));rl+=strlen(rep);
                pos+=mm.rm_eo;count++;if(!g)break;if(mm.rm_so==mm.rm_eo){if(*pos){result[rl++]=*pos++;}else break;}}
            int rest=strlen(pos);if(rl+rest+1>=rcap){rcap=rl+rest+1;result=realloc(result,rcap);}memcpy(result+rl,pos,rest);rl+=rest;result[rl]='\0';
            if(tgt_is_field){if(tgt_idx==0){free(c->line);c->line=strdup(result);split_fields(c);}else if(tgt_idx>0&&tgt_idx<=c->nf){free(c->flds[tgt_idx-1]);c->flds[tgt_idx-1]=strdup(result);}}
            free(tgt);val_t rv2=mk_num(count);free(result);return rv2;}
        if(strcmp(fn,"match")==0){val_t sv=eval_expr(p,a->ch[0],c);val_t pv=eval_expr(p,a->ch[1],c);
            if(ere_match(v_str(&pv),v_str(&sv),m,1)){c->rstart=m[0].rm_so+1;c->rlen=m[0].rm_eo-m[0].rm_so;return mk_num(c->rstart);}
            c->rstart=0;c->rlen=-1;return mk_num(0);}
        if(strcmp(fn,"tolower")==0||strcmp(fn,"toupper")==0){val_t sv=eval_expr(p,a->ch[0],c);const char *s=v_str(&sv);
            char *r=strdup(s);for(int i=0;r[i];i++)r[i]=(strcmp(fn,"tolower")==0)?tolower((unsigned char)r[i]):toupper((unsigned char)r[i]);val_t rv2=mk_str(r);free(r);return rv2;}
        if(strcmp(fn,"sin")==0){val_t __arg=eval_expr(p,a->ch[0],c);return mk_num(sin(v_num(&__arg)));}
        if(strcmp(fn,"cos")==0){val_t __arg=eval_expr(p,a->ch[0],c);return mk_num(cos(v_num(&__arg)));}
        if(strcmp(fn,"exp")==0){val_t __arg=eval_expr(p,a->ch[0],c);return mk_num(exp(v_num(&__arg)));}
        if(strcmp(fn,"log")==0){val_t __arg=eval_expr(p,a->ch[0],c);return mk_num(log(v_num(&__arg)));}
        if(strcmp(fn,"sqrt")==0){val_t __arg=eval_expr(p,a->ch[0],c);return mk_num(sqrt(v_num(&__arg)));}
        if(strcmp(fn,"int")==0){val_t __arg=eval_expr(p,a->ch[0],c);return mk_num((long long)v_num(&__arg));}
        if(strcmp(fn,"rand")==0){if(!c->rand_seed_init){srand((unsigned)time(NULL));c->rand_seed_init=1;}return mk_num((double)rand()/RAND_MAX);}
        if(strcmp(fn,"srand")==0){if(a->nc>0){val_t __arg=eval_expr(p,a->ch[0],c);srand((unsigned int)v_num(&__arg));}else srand((unsigned)time(NULL));c->rand_seed_init=1;return mk_num(0);}
        if(strcmp(fn,"getline")==0){
            char *line=NULL;size_t cap=0;ssize_t n;
            if(c->cfp&&!c->eof){n=getline(&line,&cap,c->cfp);if(n>0){if(line[n-1]=='\n')line[--n]='\0';
                free(c->line);c->line=strdup(line);if(a->nc>0&&a->ch[0]->t==AST_VAR)vset(c,a->ch[0]->s,mk_str(line));
                split_fields(c);free(line);return mk_num(1);}c->eof=1;}
            if(line)free(line);return mk_num(0);}
        if(strcmp(fn,"close")==0)return mk_num(0);
        if(strcmp(fn,"system")==0){val_t v=eval_expr(p,a->ch[0],c);return mk_num(system(v_str(&v)));}
        return mk_num(0);
    }
    default: return mk_num(0);
    }
}

static void split_fields(ctx_t *c) {
    for(int i=0;i<c->nf;i++){free(c->flds[i]);c->flds[i]=NULL;}c->nf=0;
    if(!c->line)return;
    val_t *fsv=arr_get(&c->vars,"FS");const char *fs=(fsv&&fsv->s)?v_str(fsv):(c->fs?c->fs:" ");
    int fsl=(int)strlen(fs);int sp=(strcmp(fs," ")==0);
    const char *p=c->line; if(sp)while(*p==' '||*p=='\t')p++;
    while(*p&&c->nf<MAXFLDS){char b[4096];int bi=0;
        if(sp){while(*p&&*p!=' '&&*p!='\t'&&bi<4090)b[bi++]=*p++;while(*p==' '||*p=='\t')p++;}
        else{if(fsl==1){while(*p&&*p!=*fs&&bi<4090)b[bi++]=*p++;if(*p==*fs)p++;}
            else{while(*p&&strncmp(p,fs,fsl)!=0&&bi<4090)b[bi++]=*p++;if(strncmp(p,fs,fsl)==0)p+=fsl;}}
        b[bi]='\0';c->flds[c->nf++]=strdup(b);}
}

static int pattern_match(pctx_t *p, ast_t *pat, ctx_t *c) {
    if(!pat)return 1;
    if(pat->t==AST_REGEX){regmatch_t m;return ere_match(pat->s,c->line?c->line:"",&m,1);}
    val_t v=eval_expr(p,pat,c);return v_num(&v)!=0;
}

static void exec_print(pctx_t *p, ast_t *a, ctx_t *c, int is_pf) {
    FILE *out=stdout;
    if(a->redir){val_t tv=eval_expr(p,a->opd,c);const char *fn=v_str(&tv);
        if(strcmp(a->redir,">")==0)out=fopen(fn,"w");else if(strcmp(a->redir,">>")==0)out=fopen(fn,"a");else if(strcmp(a->redir,"|")==0)out=popen(fn,"w");
        if(!out){fprintf(stderr,"awk: cannot open %s\n",fn);return;}}
    if(is_pf){if(a->nc==0)return;
        val_t fv=eval_expr(p,a->ch[0],c);const char *fmt=v_str(&fv);
        char buf[4096];int bi=0,ai=1;
        for(int i=0;fmt[i]&&bi<4090;i++){if(fmt[i]=='%'&&fmt[i+1]){
            i++;if(fmt[i]=='s'){if(ai<a->nc){val_t av=eval_expr(p,a->ch[ai],c);const char *s=v_str(&av);int sl=(int)strlen(s);memcpy(buf+bi,s,sl<4090-bi?sl:4090-bi);bi+=sl;}ai++;}
            else if(fmt[i]=='d'){if(ai<a->nc){val_t av=eval_expr(p,a->ch[ai],c);bi+=sprintf(buf+bi,"%d",(int)v_num(&av));}ai++;}
            else if(fmt[i]=='g'||fmt[i]=='f'){if(ai<a->nc){val_t av=eval_expr(p,a->ch[ai],c);bi+=sprintf(buf+bi,"%g",v_num(&av));}ai++;}
            else{buf[bi++]='%';buf[bi++]=fmt[i];}}else buf[bi++]=fmt[i];}
        buf[bi]='\0';fprintf(out,"%s",buf);
    } else {
        const char *ofs=c->ofs?c->ofs:" ";const char *ors=c->ors?c->ors:"\n";
        if(a->nc==0){fprintf(out,"%s%s",c->line?c->line:"",ors);}
        else{for(int i=0;i<a->nc;i++){if(i>0)fprintf(out,"%s",ofs);val_t v=eval_expr(p,a->ch[i],c);fprintf(out,"%s",v_str(&v));}fprintf(out,"%s",ors);}
    }
    if(a->redir){if(strcmp(a->redir,"|")==0)pclose(out);else fclose(out);}
}

static jt_t exec_stmt(pctx_t *p, ast_t *a, ctx_t *c) {
    if(!a)return JT_NONE;
    switch(a->t) {
    case AST_EMPTY:return JT_NONE;
    case AST_BLOCK:for(int i=0;i<a->nc;i++){jt_t j=exec_stmt(p,a->ch[i],c);if(j!=JT_NONE)return j;}return JT_NONE;
    case AST_EXPR_STMT:eval_expr(p,a,c);return JT_NONE;
    case AST_IF:{val_t cv=eval_expr(p,a->cond,c);if(v_num(&cv)!=0)return exec_stmt(p,a->ta,c);if(a->ea)return exec_stmt(p,a->ea,c);return JT_NONE;}
    case AST_WHILE:while(1){val_t cv=eval_expr(p,a->cond,c);if(v_num(&cv)==0)break;
        jt_t j=exec_stmt(p,a->opd,c);if(j==JT_BREAK)break;if(j==JT_CONTINUE)continue;if(j==JT_EXIT||j==JT_RETURN||j==JT_NEXT)return j;}return JT_NONE;
    case AST_DOWHILE:do{jt_t j=exec_stmt(p,a->opd,c);
        if(j==JT_BREAK)break;if(j==JT_CONTINUE){val_t cv=eval_expr(p,a->cond,c);if(v_num(&cv)==0)break;continue;}
        if(j==JT_EXIT||j==JT_RETURN||j==JT_NEXT)return j;val_t cv=eval_expr(p,a->cond,c);if(v_num(&cv)==0)break;}while(1);return JT_NONE;
    case AST_FOR:if(a->ch[0])eval_expr(p,a->ch[0],c);
        while(1){if(a->cond){val_t cv=eval_expr(p,a->cond,c);if(v_num(&cv)==0)break;}
            jt_t j=exec_stmt(p,a->ch[2],c);if(j==JT_BREAK)break;if(j==JT_EXIT||j==JT_RETURN||j==JT_NEXT)return j;
            if(a->ch[1])eval_expr(p,a->ch[1],c);if(j==JT_CONTINUE)continue;}return JT_NONE;
    case AST_FORIN:{char keys[256][1024];int nk=0;
        for(int i=0;i<64&&nk<256;i++){an_t *n=c->arrays.b[i];while(n&&nk<256){
            char px[256];snprintf(px,sizeof(px),"%s\x01",a->in_arr);if(strncmp(n->key,px,strlen(px))==0){strcpy(keys[nk],n->key+strlen(px));nk++;}n=n->next;}}
        for(int i=0;i<nk;i++){vset(c,a->in_var,mk_str(keys[i]));jt_t j=exec_stmt(p,a->opd,c);
            if(j==JT_BREAK)break;if(j==JT_CONTINUE)continue;if(j==JT_EXIT||j==JT_RETURN||j==JT_NEXT)return j;}return JT_NONE;}
    case AST_PRINT:exec_print(p,a,c,0);return JT_NONE;
    case AST_PRINTF:exec_print(p,a,c,1);return JT_NONE;
    case AST_BREAK:return JT_BREAK;case AST_CONTINUE:return JT_CONTINUE;case AST_NEXT:return JT_NEXT;
    case AST_EXIT:if(a->opd)eval_expr(p,a->opd,c);return JT_EXIT;
    case AST_RETURN:if(a->opd)eval_expr(p,a->opd,c);return JT_RETURN;
    case AST_DELETE:if(a->opd->t==AST_AREF){char key[1024]="";for(int i=0;i<a->opd->nc;i++){val_t kv=eval_expr(p,a->opd->ch[i],c);if(i>0)strcat(key,"\x01");strcat(key,v_str(&kv));}adel(c,a->opd->s,key);}
        else if(a->opd->t==AST_VAR)for(int i=0;i<64;i++){an_t **pp=&c->arrays.b[i];while(*pp){char px[256];snprintf(px,sizeof(px),"%s\x01",a->opd->s);
            if(strncmp((*pp)->key,px,strlen(px))==0){an_t *t=*pp;*pp=t->next;v_free(&t->val);free(t->key);free(t);}else pp=&(*pp)->next;}}return JT_NONE;
    default:eval_expr(p,a,c);return JT_NONE;
    }
}

static void process_record(pctx_t *p, ctx_t *c) {
    ast_t *body=p->body;
    for(int i=0;i<body->nc;i++){ast_t *a=body->ch[i];
        if(a->t==AST_ACTION){int match=1;
            if(a->ta){static int in_range=0;if(!in_range){match=pattern_match(p,a->cond,c);if(match)in_range=1;}else{match=1;if(pattern_match(p,a->ta,c))in_range=0;}}
            else match=pattern_match(p,a->cond,c);
            if(match){if(a->opd)exec_stmt(p,a->opd,c);else printf("%s\n",c->line?c->line:"");}
        }
    }
}

static void run_program(pctx_t *p, ctx_t *c) {
    if(p->begin)exec_stmt(p,p->begin,c);
    for(int fi=0;fi<c->nfiles||(c->nfiles==0&&fi==0);fi++){
        if(c->nfiles>0){c->fname=c->files[fi];c->cfp=fopen(c->fname,"r");if(!c->cfp){fprintf(stderr,"awk: cannot open %s\n",c->fname);continue;}}
        else{c->cfp=stdin;c->fname="(stdin)";}
        c->f_nr=0;c->eof=0;char *line=NULL;size_t cap=0;ssize_t n;
        while(c->cfp&&!c->eof){n=getline(&line,&cap,c->cfp);
            if(n<=0){if(c->cfp!=stdin){fclose(c->cfp);c->cfp=NULL;}break;}
            if(line[n-1]=='\n')line[--n]='\0';if(n>0&&line[n-1]=='\r')line[--n]='\0';
            free(c->line);c->line=strdup(line);c->nr++;c->f_nr++;
            split_fields(c);
            char b[64];snprintf(b,sizeof(b),"%ld",c->nr);vset(c,"NR",mk_str(b));
            snprintf(b,sizeof(b),"%ld",c->f_nr);vset(c,"FNR",mk_str(b));
            snprintf(b,sizeof(b),"%d",c->nf);vset(c,"NF",mk_str(b));
            vset(c,"FILENAME",mk_str(c->fname?c->fname:""));
            process_record(p,c);
        } if(line)free(line);
    }
    if(p->end)exec_stmt(p,p->end,c);
}

int main(int argc, char **argv) {
    ctx_t ctx; memset(&ctx,0,sizeof(ctx));
    arr_init(&ctx.vars); arr_init(&ctx.arrays);
    ctx.fs=" ";ctx.rs="\n";ctx.ofs=" ";ctx.ors="\n";
    char *prog=NULL; int palloc=0;
    int oi=1;
    while(oi<argc&&argv[oi][0]=='-'){
        if(strcmp(argv[oi],"-f")==0){oi++;if(oi>=argc)awk_die("need arg for -f");
            FILE *fp=fopen(argv[oi],"r");if(!fp)awk_die("cannot open %s",argv[oi]);
            fseek(fp,0,SEEK_END);long sz=ftell(fp);fseek(fp,0,SEEK_SET);
            prog=malloc(sz+1);palloc=1;fread(prog,1,sz,fp);prog[sz]='\0';fclose(fp);oi++;continue;}
        if(strcmp(argv[oi],"-F")==0){oi++;if(oi>=argc)awk_die("need arg for -F");ctx.fs=strdup(argv[oi]);oi++;continue;}
        if(strncmp(argv[oi],"-v",2)==0){char *sp=argv[oi]+2;if(*sp=='\0'){oi++;if(oi>=argc)awk_die("need arg for -v");sp=argv[oi];}
            char *eq=strchr(sp,'=');if(!eq)awk_die("invalid -v: %s",sp);
            char *nm=strndup(sp,eq-sp);char *vl=strdup(eq+1);vset(&ctx,nm,mk_str(vl));free(nm);free(vl);oi++;continue;}
        if(strcmp(argv[oi],"--")==0){oi++;break;}
        if(strcmp(argv[oi],"--help")==0){printf("Usage: awk [-F fs] [-v var=val] [-f progfile | 'program'] [file ...]\n");return 0;}
        if(strcmp(argv[oi],"--version")==0){printf("awk 0.1.0 (meuos-utils)\n");return 0;}
        break;
    }
    if(!prog&&oi<argc){prog=argv[oi];oi++;}
    if(!prog)awk_die("no program");
    ctx.files=argv+oi;ctx.nfiles=argc-oi;
    lex_t lex;lex_init(&lex,prog);pctx_t pctx;memset(&pctx,0,sizeof(pctx));pctx.l=&lex;
    parse_program(&pctx); run_program(&pctx,&ctx); 
    if(palloc)free(prog);
    afree(pctx.begin);
    afree(pctx.end);
    afree(pctx.body);
    arr_free(&ctx.vars);
    arr_free(&ctx.arrays);
    return 0;
}
