#include <stdio.h>
int fib(int n){ return n<2 ? n : fib(n-1)+fib(n-2); }
int main(void){ printf("fib(10)=%d\n", fib(10)); return fib(10)==55 ? 0 : 1; }
