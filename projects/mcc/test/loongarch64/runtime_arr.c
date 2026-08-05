/* runtime_arr.c — loongarch64 runtime regression: a statically-linked,
 * runnable MeuOS binary exercising the global-array address path plus a
 * small ABI width-arithmetic check, matching the static-array and basic-ABI
 * coverage expected of an architecture runtime batch.
 *
 * Exit 0 on success; nonzero per failed check.  Requires a loongarch64
 * user-mode qemu to execute (run by test/loongarch64/runtime.sh when
 * present).
 */
int gdata[4] = {5, 6, 7, 8};
int gbss[6];
int *gp = gdata;

int
la64_abi(int value)
{
	unsigned char byte = value;
	short half = value;
	int word = value;
	return (int)byte + (int)half + word;
}

int
main(void)
{
	int i;

	if (gdata[2] != 7) return 1;
	gdata[0] = 88;
	if (gdata[0] != 88) return 2;

	for (i = 0; i < 6; ++i)          /* .bss index-scaled writes */
		gbss[i] = i * 9;
	if (gbss[5] != 45) return 3;

	if (*gp != 88) return 4;         /* global pointer follows array */
	if (la64_abi(0x12345678) != (int)(0x78 + 0x5678 + 0x12345678)) return 5;

	return 0;
}
