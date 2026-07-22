#ifndef MT_AS_H
#define MT_AS_H

/* x86_64-first assembler entry point. */
int mt_as_assemble(const char *input, const char *output,
                   const char **error_message, unsigned *error_line);

#endif
