/* runtime_arr.c — riscv64 runtime regression: a statically-linked, runnable
 * MeuOS binary exercising the global-array address path plus a small ABI
 * width-arithmetic check.
 *
 * The global-array here is the point: riscv64 non-PIC global data used to
 * emit `%pcrel_lo(sym)` (unpaired, link error `%pcrel_lo missing matching
 * %pcrel_hi`), fixed to a paired `%pcrel_lo(.Lrvpc)` referencing the lui's
 * label (riscv64_memit.c emit_global_addr).
 *
 * Exit 0 on success; nonzero per failed check.  Requires a riscv64
 * user-mode qemu to execute (run by test/riscv64/runtime.sh when present).
 */
int gdata[4] = {11, 22, 33, 44};
int gbss[6];
int *gp = gdata;

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
	int i;

	if (gdata[2] != 33) return 1;
	gdata[0] = 99;
	if (gdata[0] != 99) return 2;

	for (i = 0; i < 6; ++i)         /* .bss index-scaled writes */
		gbss[i] = i * 7;
	if (gbss[4] != 28) return 3;

	if (*gp != 99) return 4;        /* global pointer follows array */
	if (rv64_abi(0x12345678) != (int)(0x78 + 0x5678 + 0x12345678)) return 5;

	return 0;
}
