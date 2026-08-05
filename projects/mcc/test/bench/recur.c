/* 函数调用密集：递归 + 尾递归 */
#include <stdio.h>
int fib(int n) { return n < 2 ? n : fib(n-1) + fib(n-2); }
long sumn(long n, long acc) { return n ? sumn(n-1, acc+n) : acc; }
int gcd(int a, int b) { return b ? gcd(b, a % b) : a; }
int main(void) {
  int f = 0;
  for (int r = 0; r < 5; r++) f += fib(30);
  long s = sumn(20000, 0);
  int g = 0;
  for (int r = 0; r < 1000000; r++) g += gcd(r, 12345);
  printf("%d %ld %d\n", f, s, g);
  return 0;
}
