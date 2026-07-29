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
#elif defined(__aarch64__)
/* aarch64 setjmp.S 保存 x19-x30、sp、d8-d15 共 22 个字；sigjmp_buf
 * 额外追加了 mask-saved 标志位与 sigset_t，共 24 个字。 */
typedef unsigned long jmp_buf[22];
#ifndef __STRICT_ANSI__
typedef unsigned long sigjmp_buf[24];
#endif
#elif defined(__riscv)
/* riscv64 LP64D（硬浮点 ABI）：setjmp.S 保存 s0-s11、sp、ra 共 14 字，
 * 以及 fs0-fs11 共 12 字，合计 26 字；sigjmp_buf 追加 mask 标志位与
 * sigset_t（各 1 字），共 28 字。 */
typedef unsigned long jmp_buf[26];
#ifndef __STRICT_ANSI__
typedef unsigned long sigjmp_buf[28];
#endif
#elif defined(__loongarch64)
/* loongarch64 LP64D（硬浮点 ABI）：setjmp.S 保存 $s0-$s8、$fp、$sp、$ra 共 12 字，
 * 以及 $fs0-$fs7 共 8 字，合计 20 字 + 3 预留 = 23 字；sigjmp_buf 追加 mask 标志位与
 * sigset_t（各 1 字），共 25 字。 */
typedef unsigned long jmp_buf[23];
#ifndef __STRICT_ANSI__
typedef unsigned long sigjmp_buf[25];
#endif
#else
/* x86_64: setjmp.S saves rbx/rbp/r12/r13/r14/r15/rip/rsp = 8 words */
typedef unsigned long jmp_buf[8];
#ifndef __STRICT_ANSI__
typedef unsigned long sigjmp_buf[10];
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
