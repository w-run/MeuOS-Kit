#ifndef MT_LD_H
#define MT_LD_H

#include <stddef.h>

/* Static linker entry point.
 *
 * target  is the architecture name ("x86_64", …) or NULL to default to x86_64.
 * All other architectures now parse correctly to set ELF header fields,
 * but relocation support is currently limited to x86_64. */
int mt_ld_link(const char *output, const char *entry,
               const char *const *inputs, size_t input_count,
               const char *target,
               const char **error_message);

#endif
