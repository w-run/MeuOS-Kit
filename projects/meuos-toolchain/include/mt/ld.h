#ifndef MT_LD_H
#define MT_LD_H

#include <stddef.h>

/* x86_64 static linker entry point. */
int mt_ld_link(const char *output, const char *entry,
               const char *const *inputs, size_t input_count,
               const char **error_message);

#endif
