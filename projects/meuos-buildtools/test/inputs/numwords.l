%{
#include <stdio.h>
%}

%%

[0-9]+          { printf("NUM:%s ", yytext); }
[a-zA-Z]+       { printf("WORD:%s ", yytext); }
[ \t\n]         { /* skip whitespace */ }
.               { printf("CH:%s ", yytext); }

%%

int main(void) {
    while (yylex() > 0);
    printf("\n");
    return 0;
}