#include <stdio.h>
struct S { int a; long b; char c; };
static struct S mk(int a, long b, char c){ struct S s = {a,b,c}; return s; }
static long sum(struct S s){ return s.a + s.b + s.c; }
int main(void){ struct S s = mk(3, 4L, 5); return sum(s)==12 ? 0 : 1; }
