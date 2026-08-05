/* 字符串：手写 strlen/strcmp/memcpy 风格 */
#include <stdio.h>
#define N 200000
static char buf[N];
static char pat[32] = "The quick brown fox jumps";
int my_strlen(const char *s) { int n = 0; while (s[n]) n++; return n; }
int my_strcmp(const char *a, const char *b) {
  while (*a && *a == *b) { a++; b++; }
  return (unsigned char)*a - (unsigned char)*b;
}
void my_memcpy(char *d, const char *s, int n) {
  for (int i = 0; i < n; i++) d[i] = s[i];
}
int main(void) {
  for (int i = 0; i < N; i++) buf[i] = "abcdefghij"[i % 10];
  buf[N-1] = 0;
  long len = 0;
  for (int r = 0; r < 2000; r++) len += my_strlen(buf);
  int cmp = 0;
  for (int r = 0; r < 2000; r++) cmp += my_strcmp(buf, buf);
  char dst[N];
  for (int r = 0; r < 2000; r++) my_memcpy(dst, buf, N);
  printf("%ld %d %d\n", len, cmp, dst[100]);
  return 0;
}
