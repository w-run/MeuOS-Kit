#ifndef MT_AS_H
#define MT_AS_H

/* Assembler entry point.
 *
 * target  is the architecture name ("x86_64", …) or NULL to default to x86_64.
 * march   is an optional -march= value (e.g. "x86-64-v3") used to enable ISA
 *         extensions; NULL keeps the architecture baseline.  All other
 *         architectures now parse correctly to set ELF header fields, but
 *         encoding support is currently limited to x86_64. */
int mt_as_assemble(const char *input, const char *output,
                   const char *target, const char *march,
                   const char **error_message, unsigned *error_line);

#endif
