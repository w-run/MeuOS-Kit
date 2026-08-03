/* 综合：数组排序（插入 + 快排） */
#include <stdio.h>
#define N 20000
static int arr[N];
void isort(int *a, int n) {
  for (int i = 1; i < n; i++) {
    int v = a[i], j = i - 1;
    while (j >= 0 && a[j] > v) { a[j+1] = a[j]; j--; }
    a[j+1] = v;
  }
}
void qsort_(int *a, int lo, int hi) {
  while (lo < hi) {
    int p = a[(lo + hi) / 2], i = lo, j = hi;
    while (i <= j) {
      while (a[i] < p) i++;
      while (a[j] > p) j--;
      if (i <= j) { int t = a[i]; a[i] = a[j]; a[j] = t; i++; j--; }
    }
    if (j - lo < hi - i) { qsort_(a, lo, j); lo = i; } else { qsort_(a, i, hi); hi = j; }
  }
}
int main(void) {
  static int tmp[N];
  for (int r = 0; r < 50; r++) {
    for (int i = 0; i < N; i++) tmp[i] = (i * 2654435761u) % N;
    qsort_(tmp, 0, N-1);
  }
  for (int r = 0; r < 5; r++) {
    for (int i = 0; i < N; i++) arr[i] = N - i;
    isort(arr, N);
  }
  printf("%d %d\n", tmp[0], arr[0]);
  return 0;
}
