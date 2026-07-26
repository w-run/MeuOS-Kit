.arm
.text

/* MeuOS compiler atomic runtime, ARMv7 hard-float (AAPCS).
 *
 * ARMv7 has no native load-acquire/store-release instructions (those
 * are ARMv8+).  We implement acquire/release semantics using:
 *   - dmb ish: full memory barrier (sync all loads/stores)
 *   - ldrex/strex: exclusive load/store for LL/SC (RMW) loops
 *
 * For plain atomic loads/stores (not RMW), we use dmb barriers:
 *   load-acquire:  ldr ; dmb ish
 *   store-release: dmb ish ; str
 *
 * For RMW (exchange, fetch_*, CAS):
 *   ldrex/strex loop with dmb after success
 *
 * NO 8-byte atomics: ARMv7 ldrexd/strexd are complex (clobber r0-r3)
 * and not needed for initial port.  Skip for now (linker error if used).
 *
 * Symbol set matches src/arch/aarch64/atomic.S so the rest of libc
 * links identically across architectures. */

.globl atomic_thread_fence
.type atomic_thread_fence,@function
atomic_thread_fence:
	dmb ish
	bx lr

.globl atomic_signal_fence
.type atomic_signal_fence,@function
atomic_signal_fence:
	bx lr

/* ===== 1-byte (byte) ===== */

.globl __atomic_load_1
.type __atomic_load_1,@function
__atomic_load_1:
	ldrb r0, [r0]
	dmb ish
	bx lr

.globl __atomic_store_1
.type __atomic_store_1,@function
__atomic_store_1:
	dmb ish
	strb r1, [r0]
	dmb ish
	bx lr

.globl __atomic_exchange_1
.type __atomic_exchange_1,@function
__atomic_exchange_1:
	dmb ish
1:	ldrexb r2, [r0]
	strexb r3, r1, [r0]
	cmp r3, #0
	bne 1b
	dmb ish
	mov r0, r2
	bx lr

.globl __atomic_fetch_add_1
.type __atomic_fetch_add_1,@function
__atomic_fetch_add_1:
	dmb ish
1:	ldrexb r2, [r0]
	add r3, r2, r1
	strexb r4, r3, [r0]
	cmp r4, #0
	bne 1b
	dmb ish
	mov r0, r2
	bx lr

.globl __atomic_fetch_sub_1
.type __atomic_fetch_sub_1,@function
__atomic_fetch_sub_1:
	dmb ish
1:	ldrexb r2, [r0]
	sub r3, r2, r1
	strexb r4, r3, [r0]
	cmp r4, #0
	bne 1b
	dmb ish
	mov r0, r2
	bx lr

.globl __atomic_fetch_and_1
.type __atomic_fetch_and_1,@function
__atomic_fetch_and_1:
	dmb ish
1:	ldrexb r2, [r0]
	and r3, r2, r1
	strexb r4, r3, [r0]
	cmp r4, #0
	bne 1b
	dmb ish
	mov r0, r2
	bx lr

.globl __atomic_fetch_or_1
.type __atomic_fetch_or_1,@function
__atomic_fetch_or_1:
	dmb ish
1:	ldrexb r2, [r0]
	orr r3, r2, r1
	strexb r4, r3, [r0]
	cmp r4, #0
	bne 1b
	dmb ish
	mov r0, r2
	bx lr

.globl __atomic_fetch_xor_1
.type __atomic_fetch_xor_1,@function
__atomic_fetch_xor_1:
	dmb ish
1:	ldrexb r2, [r0]
	eor r3, r2, r1
	strexb r4, r3, [r0]
	cmp r4, #0
	bne 1b
	dmb ish
	mov r0, r2
	bx lr

.globl __atomic_compare_exchange_1
.type __atomic_compare_exchange_1,@function
__atomic_compare_exchange_1:
	ldrb r3, [r1]
	dmb ish
1:	ldrexb r4, [r0]
	cmp r4, r3
	bne 2f
	strexb r5, r2, [r0]
	cmp r5, #0
	bne 1b
	dmb ish
	mov r0, #1
	bx lr
2:	dmb ish
	strb r4, [r1]
	mov r0, #0
	bx lr

/* ===== 2-byte (halfword) ===== */

.globl __atomic_load_2
.type __atomic_load_2,@function
__atomic_load_2:
	ldrh r0, [r0]
	dmb ish
	bx lr

.globl __atomic_store_2
.type __atomic_store_2,@function
__atomic_store_2:
	dmb ish
	strh r1, [r0]
	dmb ish
	bx lr

.globl __atomic_exchange_2
.type __atomic_exchange_2,@function
__atomic_exchange_2:
	dmb ish
1:	ldrexh r2, [r0]
	strexh r3, r1, [r0]
	cmp r3, #0
	bne 1b
	dmb ish
	mov r0, r2
	bx lr

.globl __atomic_fetch_add_2
.type __atomic_fetch_add_2,@function
__atomic_fetch_add_2:
	dmb ish
1:	ldrexh r2, [r0]
	add r3, r2, r1
	strexh r4, r3, [r0]
	cmp r4, #0
	bne 1b
	dmb ish
	mov r0, r2
	bx lr

.globl __atomic_fetch_sub_2
.type __atomic_fetch_sub_2,@function
__atomic_fetch_sub_2:
	dmb ish
1:	ldrexh r2, [r0]
	sub r3, r2, r1
	strexh r4, r3, [r0]
	cmp r4, #0
	bne 1b
	dmb ish
	mov r0, r2
	bx lr

.globl __atomic_fetch_and_2
.type __atomic_fetch_and_2,@function
__atomic_fetch_and_2:
	dmb ish
1:	ldrexh r2, [r0]
	and r3, r2, r1
	strexh r4, r3, [r0]
	cmp r4, #0
	bne 1b
	dmb ish
	mov r0, r2
	bx lr

.globl __atomic_fetch_or_2
.type __atomic_fetch_or_2,@function
__atomic_fetch_or_2:
	dmb ish
1:	ldrexh r2, [r0]
	orr r3, r2, r1
	strexh r4, r3, [r0]
	cmp r4, #0
	bne 1b
	dmb ish
	mov r0, r2
	bx lr

.globl __atomic_fetch_xor_2
.type __atomic_fetch_xor_2,@function
__atomic_fetch_xor_2:
	dmb ish
1:	ldrexh r2, [r0]
	eor r3, r2, r1
	strexh r4, r3, [r0]
	cmp r4, #0
	bne 1b
	dmb ish
	mov r0, r2
	bx lr

.globl __atomic_compare_exchange_2
.type __atomic_compare_exchange_2,@function
__atomic_compare_exchange_2:
	ldrh r3, [r1]
	dmb ish
1:	ldrexh r4, [r0]
	cmp r4, r3
	bne 2f
	strexh r5, r2, [r0]
	cmp r5, #0
	bne 1b
	dmb ish
	mov r0, #1
	bx lr
2:	dmb ish
	strh r4, [r1]
	mov r0, #0
	bx lr

/* ===== 4-byte (word) ===== */

.globl __atomic_load_4
.type __atomic_load_4,@function
__atomic_load_4:
	ldr r0, [r0]
	dmb ish
	bx lr

.globl __atomic_store_4
.type __atomic_store_4,@function
__atomic_store_4:
	dmb ish
	str r1, [r0]
	dmb ish
	bx lr

.globl __atomic_exchange_4
.type __atomic_exchange_4,@function
__atomic_exchange_4:
	dmb ish
1:	ldrex r2, [r0]
	strex r3, r1, [r0]
	cmp r3, #0
	bne 1b
	dmb ish
	mov r0, r2
	bx lr

.globl __atomic_fetch_add_4
.type __atomic_fetch_add_4,@function
__atomic_fetch_add_4:
	dmb ish
1:	ldrex r2, [r0]
	add r3, r2, r1
	strex r4, r3, [r0]
	cmp r4, #0
	bne 1b
	dmb ish
	mov r0, r2
	bx lr

.globl __atomic_fetch_sub_4
.type __atomic_fetch_sub_4,@function
__atomic_fetch_sub_4:
	dmb ish
1:	ldrex r2, [r0]
	sub r3, r2, r1
	strex r4, r3, [r0]
	cmp r4, #0
	bne 1b
	dmb ish
	mov r0, r2
	bx lr

.globl __atomic_fetch_and_4
.type __atomic_fetch_and_4,@function
__atomic_fetch_and_4:
	dmb ish
1:	ldrex r2, [r0]
	and r3, r2, r1
	strex r4, r3, [r0]
	cmp r4, #0
	bne 1b
	dmb ish
	mov r0, r2
	bx lr

.globl __atomic_fetch_or_4
.type __atomic_fetch_or_4,@function
__atomic_fetch_or_4:
	dmb ish
1:	ldrex r2, [r0]
	orr r3, r2, r1
	strex r4, r3, [r0]
	cmp r4, #0
	bne 1b
	dmb ish
	mov r0, r2
	bx lr

.globl __atomic_fetch_xor_4
.type __atomic_fetch_xor_4,@function
__atomic_fetch_xor_4:
	dmb ish
1:	ldrex r2, [r0]
	eor r3, r2, r1
	strex r4, r3, [r0]
	cmp r4, #0
	bne 1b
	dmb ish
	mov r0, r2
	bx lr

.globl __atomic_compare_exchange_4
.type __atomic_compare_exchange_4,@function
__atomic_compare_exchange_4:
	ldr r3, [r1]
	dmb ish
1:	ldrex r4, [r0]
	cmp r4, r3
	bne 2f
	strex r5, r2, [r0]
	cmp r5, #0
	bne 1b
	dmb ish
	mov r0, #1
	bx lr
2:	dmb ish
	str r4, [r1]
	mov r0, #0
	bx lr

.section .note.GNU-stack,"",@progbits
