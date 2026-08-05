/* 浮点密集：矩阵乘 + 多项式 */
#include <stdio.h>
#define M 64
static double A[M][M], B[M][M], C[M][M];
void matmul(void) {
  for (int i = 0; i < M; i++)
    for (int k = 0; k < M; k++) {
      double a = A[i][k];
      for (int j = 0; j < M; j++)
        C[i][j] += a * B[k][j];
    }
}
double poly(double x) {
  double r = 0;
  for (int i = 0; i < 20; i++) r = r * x + (i + 1);
  return r;
}
int main(void) {
  for (int i = 0; i < M; i++)
    for (int j = 0; j < M; j++) { A[i][j] = (i*M+j)*0.001; B[i][j] = (j*M+i)*0.002; }
  for (int r = 0; r < 200; r++) matmul();
  double p = 0;
  for (int r = 0; r < 1000000; r++) p += poly(r * 0.001);
  printf("%f %f\n", C[M-1][M-1], p);
  return 0;
}
