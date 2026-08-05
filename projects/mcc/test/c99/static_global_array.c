/* static_global_array.c — regression: static exe + global array segfault.
 *
 * Root cause (dfcc0dc): x86_64 emit of `imulq $4,%rax` (array-index scaling)
 * encoded the imm as a reg/mem operand + a stray R_X86_64_PC32 reloc, shifting
 * instructions and faulting at runtime (139).  Exercise global arrays through
 * reads, writes, a global pointer to the array, pointer arithmetic, and
 * multi-initializer data + .bss so both the data-absolute and index-scaled
 * address paths are hit.
 *
 * Returns 0 on success, a distinct exit code per failure; run via check-c99.
 */

int gdata[4]  = {10, 20, 30, 40};   /* .data, initialized */
int gbss[8];                        /* .bss, zero-init */
int *gp = gdata;                    /* global pointer to a global array */

int main(void)
{
    int i;

    /* reads from an initialized global array */
    if (gdata[1] != 20) return 1;
    if (gdata[3] != 40) return 2;

    /* writes into .data and reads back */
    gdata[0] = 77;
    if (gdata[0] != 77) return 3;

    /* .bss global array: index-scaled writes/reads (exercises imul $imm) */
    for (i = 0; i < 8; ++i)
        gbss[i] = i * 3;
    if (gbss[5] != 15) return 4;
    if (gbss[0] != 0) return 5;

    /* a global pointer to the array follows the array address */
    if (*gp != 77) return 6;          /* gdata[0] was overwritten above */
    if (gp[3] != 40) return 7;

    /* pointer arithmetic / decay */
    if (*(gdata + 1) != 20) return 8;

    return 0;
}
