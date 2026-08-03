/* 整数密集：循环 + 数组 + 算术 */
#include <stdio.h>
#define N 100000000
long sink;
long sum_arr(int *a, int n) {
  long s = 0;
  for (int i = 0; i < n; i++) s += a[i];
  return s;
}
long mul_acc(long x, int n) {
  long s = 0;
  for (int i = 0; i < n; i++) s = (s + x) * 3 - x / 7;
  return s;
}
int main(void) {
  static int a[1024];
  for (int i = 0; i < 1024; i++) a[i] = i % 37;
  long s = 0;
  for (int r = 0; r < N / 1024; r++) s += sum_arr(a, 1024);
  s += mul_acc(12345, N / 10);
  sink = s;
  printf("%ld\n", s);
  return 0;
}
