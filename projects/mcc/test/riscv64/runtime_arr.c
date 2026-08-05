/* runtime_arr.c — riscv64 runtime regression: a statically-linked, runnable
 * MeuOS binary exercising index-scaled stack/data access plus a small ABI
 * width-arithmetic check.  NOTE: a *global* array on riscv64 currently trips
 * the assembler's `%pcrel_lo`/`%pcrel_hi` match check (mcc riscv64 emit of
 * global PC-relative data — the static-array analog tracked separately for
 * x86_64), so this runtime source deliberately uses a local array so the
 * binary still links; global-array ABI is covered by the x86_64 and
 * loongarch64 runtime batches.
 *
 * Exit 0 on success; nonzero per failed check.  Requires a riscv64
 * user-mode qemu to execute (run by test/riscv64/runtime.sh when present).
 */
int
rv64_abi(int value)
{
	unsigned char byte = value;
	short half = value;
	int word = value;
	return (int)byte + (int)half + word;
}

int
main(void)
{
	int a[6];
	int i;

	for (i = 0; i < 6; ++i)         /* index-scaled stack array writes */
		a[i] = i * 7;
	if (a[4] != 28) return 1;
	if (a[0] != 0) return 2;

	if (rv64_abi(0x12345678) != (int)(0x78 + 0x5678 + 0x12345678)) return 3;

	return 0;
}
