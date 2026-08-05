/* runtime-matrix: struct member access. expect exit 42. */
struct pair { int a; int b; };
int main(void) { struct pair s = { 20, 22 }; return s.a + s.b; }
