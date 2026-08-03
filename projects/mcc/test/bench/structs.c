/* 指针/结构：链表 + 二叉搜索树 */
#include <stdio.h>
#include <stdlib.h>
typedef struct Node { int val; struct Node *next; } Node;
typedef struct TN { int val; struct TN *l, *r; } TN;
Node *mklist(int n) {
  Node *h = 0;
  for (int i = n; i > 0; i--) {
    Node *p = malloc(sizeof *p);
    p->val = i; p->next = h; h = p;
  }
  return h;
}
long listsum(Node *h) {
  long s = 0;
  for (; h; h = h->next) s += h->val;
  return s;
}
TN *tinsert(TN *t, int v) {
  if (!t) { TN *p = malloc(sizeof *p); p->val = v; p->l = p->r = 0; return p; }
  if (v < t->val) t->l = tinsert(t->l, v);
  else t->r = tinsert(t->r, v);
  return t;
}
int tlookup(TN *t, int v) {
  while (t) {
    if (v == t->val) return 1;
    t = v < t->val ? t->l : t->r;
  }
  return 0;
}
int main(void) {
  Node *l = mklist(100000);
  long s = 0;
  for (int r = 0; r < 50; r++) s += listsum(l);
  TN *t = 0;
  for (int i = 0; i < 100000; i++) t = tinsert(t, (i * 2654435761u) % 100000);
  int f = 0;
  for (int r = 0; r < 5000; r++) for (int i = 0; i < 1000; i++) f += tlookup(t, i);
  printf("%ld %d\n", s, f);
  return 0;
}
