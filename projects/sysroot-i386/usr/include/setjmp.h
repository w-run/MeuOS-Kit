#ifndef MEUOS_SETJMP_H
#define MEUOS_SETJMP_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The buffer is opaque.  i386 stores ebx/esi/edi/ebp, the caller stack
 * pointer and return address; 64-bit targets store their native callee-save
 * set. */
#if defined(__i386__)
typedef unsigned long jmp_buf[6];
#ifndef __STRICT_ANSI__
typedef unsigned long sigjmp_buf[10];
#endif
#else
typedef unsigned long jmp_buf[8];
#ifndef __STRICT_ANSI__
typedef unsigned long sigjmp_buf[16];
#endif
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
