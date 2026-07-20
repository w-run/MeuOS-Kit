#ifndef MEUOS_SETJMP_H
#define MEUOS_SETJMP_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* x86_64: rbx, rbp, r12, r13, r14, r15, rsp, rip.  The buffer is opaque;
 * the layout matches the implementation in src/setjmp/setjmp.S and must not
 * be inspected by portable code. */
typedef unsigned long jmp_buf[8];

#ifndef __STRICT_ANSI__
typedef unsigned long sigjmp_buf[16];
#endif

int setjmp(jmp_buf);
_Noreturn void longjmp(jmp_buf, int);

#ifndef __STRICT_ANSI__
int _setjmp(jmp_buf);
_Noreturn void _longjmp(jmp_buf, int);
int sigsetjmp(sigjmp_buf, int);
_Noreturn void siglongjmp(sigjmp_buf, int);
#endif

#ifdef __cplusplus
}
#endif

#endif
