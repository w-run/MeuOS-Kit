/* A 16-byte aggregate whose first two declarations share one unsigned
 * storage unit.  Passing it by value exercises both IR aggregate layout and
 * the x86_64 SysV register classifier. */
struct instruction {
	unsigned int opcode : 30;
	unsigned int cls : 2;
	unsigned int to;
	unsigned int arg0;
	unsigned int arg1;
};

static struct instruction
rewrite(struct instruction in)
{
	in.to += in.opcode;
	in.arg0 ^= in.cls;
	in.arg1 += 1;
	return in;
}

static struct instruction initial = { 7, 3, 10, 8, 9 };

int
main(void)
{
	struct instruction after = rewrite(initial);

	return after.opcode != 7 || after.cls != 3 || after.to != 17 ||
		after.arg0 != 11 || after.arg1 != 10;
}
