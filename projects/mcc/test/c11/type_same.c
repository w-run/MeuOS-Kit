/* Test typesame via _Generic */
int main(void) {
	int x = 0;
	long y = 0;
	(void)x;
	(void)y;
	/* _Generic should distinguish int from long */
	int r = _Generic(x, int: 1, long: 2, default: 0);
	return r == 1 ? 0 : 1;
}
