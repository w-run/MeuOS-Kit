#ifndef M4_ENGINE_H
#define M4_ENGINE_H

#include <stddef.h>

/* Initialize m4 engine with builtins */
void m4_init(void);

/* Process input string through m4, write output to buffer */
/* Returns 0 on success, -1 on error */
int m4_process(const char *input, char *output, size_t outsz);

/* Define a macro before processing */
void m4_define(const char *name, const char *value);

/* Set input file (for __file__ tracking) */
void m4_set_file(const char *filename);

/* Reset engine state */
void m4_reset(void);

#endif
