#ifndef MEUOS_CPUID_H
#define MEUOS_CPUID_H

/* Stub for GCC's cpuid.h. mcc does not support __get_cpuid builtins. */

static inline int __get_cpuid(unsigned int level, unsigned int *a, unsigned int *b, unsigned int *c, unsigned int *d) {
    (void)level; (void)a; (void)b; (void)c; (void)d;
    return 0;
}

#define __cpuid(level, a, b, c, d) do { (void)level; (void)a; (void)b; (void)c; (void)d; } while(0)
#define __cpuid_count(level, count, a, b, c, d) do { (void)level; (void)count; (void)a; (void)b; (void)c; (void)d; } while(0)

#endif
