%{
/* Simple calculator parser */
#include <stdio.h>
int yylex(void) {
    int c = getchar();
    if (c >= '0' && c <= '9') { ungetc(c, stdin); scanf("%d", &yylval); return NUMBER; }
    if (c == '+') return PLUS;
    if (c == '*') return STAR;
    if (c == '\n') return 0;
    return c;
}
void yyerror(const char *s) { fprintf(stderr, "%s\n", s); }
%}

%token NUMBER
%left PLUS
%left STAR

%%

expr: expr PLUS expr   { $$ = $1 + $3; }
    | expr STAR expr   { $$ = $1 * $3; }
    | NUMBER           { $$ = $1; }
    ;

%%
int main(void) { return yyparse(); }