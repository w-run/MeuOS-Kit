/* runtime-matrix: pointer dereference. expect exit 42. */
int main(void) { int x = 42; int *p = &x; return *p; }
