#ifndef MEUOS_SYS_AUXV_H
#define MEUOS_SYS_AUXV_H

#include <errno.h>

/* Auxiliary vector (auxv) constants, as defined by the Linux ELF ABI.
 * The kernel supplies an array of { type, value } pairs at program entry,
 * immediately after envp[].  <sys/auxv.h> exposes them via getauxval(3). */

#define AT_NULL		0	/* end of vector */
#define AT_IGNORE	1	/* entry should be ignored */
#define AT_EXECFD	2	/* file descriptor of program */
#define AT_PHDR		3	/* program headers for program */
#define AT_PHENT	4	/* size of program header entry */
#define AT_PHNUM	5	/* number of program headers */
#define AT_PAGESZ	6	/* system page size */
#define AT_BASE		7	/* base address of interpreter */
#define AT_FLAGS	8	/* flags */
#define AT_ENTRY	9	/* entry point of program */
#define AT_NOTELF	10	/* program is not ELF */
#define AT_UID		11	/* real uid */
#define AT_EUID		12	/* effective uid */
#define AT_GID		13	/* real gid */
#define AT_EGID		14	/* effective gid */
#define AT_PLATFORM	15	/* string identifying platform */
#define AT_HWCAP	16	/* machine-dependent hints about CPU */
#define AT_CLKTCK	17	/* frequency of times() */
#define AT_FPUCW	18	/* used FPU control word */
#define AT_DCACHEBSIZE	19	/* data cache block size */
#define AT_ICACHEBSIZE	20	/* instruction cache block size */
#define AT_UCACHEBSIZE	21	/* unified cache block size */
#define AT_IGNOREPPC	22	/* entry should be ignored (ppc) */
#define AT_SECURE	23	/* boolean: secure mode requested */
#define AT_BASE_PLATFORM	24	/* string identifying real platform */
#define AT_RANDOM	25	/* address of 16 random bytes */
#define AT_HWCAP2	26	/* more machine-dependent hints about CPU */
#define AT_RSEQ_FEATURE_SIZE	27	/* rseq supported feature size */
#define AT_RSEQ_ALIGN	28	/* rseq allocation alignment */
#define AT_EXECFN	31	/* filename of executable */

/* Atomically return the value associated with auxv entry TYPE, or 0 (and
 * errno=ENOENT) if TYPE is absent. */
unsigned long getauxval(unsigned long type);

#endif
