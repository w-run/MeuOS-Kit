#ifndef MEUOS_SYS_PRCTL_H
#define MEUOS_SYS_PRCTL_H

/* Minimal subset for libiberty's setproctitle. prctl() is rarely invoked
 * here (it's only used for PR_SET_NAME/PR_GET_NAME on Linux), so provide
 * the prototype and key constants only. */
#include <stddef.h>

#define PR_SET_NAME 15
#define PR_GET_NAME 16

int prctl(int, ...);

#endif
