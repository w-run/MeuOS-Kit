#include <stdio.h>
typedef struct { int a[3]; double d; } Big; /* 超过 16 字节 -> sret */
static Big mk(void){ Big b = {{1,2,3}, 4.0}; return b; }
static int chk(Big b){ return b.a[0]+b.a[1]+b.a[2]==6 && b.d==4.0 ? 0 : 1; }
int main(void){ return chk(mk()); }
