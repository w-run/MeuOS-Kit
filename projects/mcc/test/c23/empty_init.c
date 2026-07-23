/* Test C23 empty initializer {} */
int x = {};
int arr[10] = {};

struct S { int a; int b; };
struct S s = {};

int main(void) {
	/* scalar type */
	int a = {};
	if (a != 0) return 1;

	/* array */
	int b[10] = {};
	for (int i = 0; i < 10; i++)
		if (b[i] != 0) return 2;

	/* struct */
	struct S c = {};
	if (c.a != 0 || c.b != 0) return 3;

	/* global variables */
	if (x != 0) return 4;
	if (s.a != 0 || s.b != 0) return 5;
	for (int i = 0; i < 10; i++)
		if (arr[i] != 0) return 6;

	return 0;
}
