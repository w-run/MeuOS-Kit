/* runtime-matrix: function call + args. expect exit 42. */
static int add(int a, int b) { return a + b; }
int main(void) { return add(20, 22); }
