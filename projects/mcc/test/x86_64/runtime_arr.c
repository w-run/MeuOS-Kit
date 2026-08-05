/* runtime_arr.c — x86_64 static-exe runtime batch: exercises the
 * static-global-array address path (the static-array-segfault regression),
 * plus integer arithmetic and an initialized .data list, run as a
 * statically-linked MeuOS binary.
 *
 * Exit 0 on success; distinct nonzero per failed check.
 */
int gdata[4] = {10, 20, 30, 40};
int gbss[8];
int *gp = gdata;

int main(void)
{
    int i;

    if (gdata[2] != 30) return 1;
    gdata[0] = 99;
    if (gdata[0] != 99) return 2;

    for (i = 0; i < 8; ++i)        /* .bss index-scaled writes (imul $imm) */
        gbss[i] = i * 5;
    if (gbss[3] != 15) return 3;

    if (*gp != 99) return 4;       /* global pointer follows the array */
    if (*(gdata + 1) != 20) return 5;

    return 0;
}
