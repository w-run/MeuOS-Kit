.text

/* MeuOS compiler atomic runtime, AArch64 AAPCS64.
 *
 * Provides the __atomic_* compiler-runtime ABI that mcc lowers C11 atomics
 * into.  AArch64's memory model gives us:
 *   - ldar/stlr: single-copy-acquire load / single-copy-release store for
 *     plain loads/stores (no exclusive monitor needed).
 *   - ldaxr/stlxr: load-acquire/store-release exclusive pair implementing
 *     the ll/sc retry loop for RMW operations.
 *   - dmb ish: full barrier for atomic_thread_fence.
 *
 * Symbol set matches src/arch/x86_64/atomic.S so the rest of libc and
 * applications link identically across architectures. */

.globl atomic_thread_fence
.type atomic_thread_fence,@function
atomic_thread_fence:
	dmb ish
	ret

.globl atomic_signal_fence
.type atomic_signal_fence,@function
atomic_signal_fence:
	ret

/* ===== 1-byte (byte) =====
 * ldarb/stlrb operate on W registers, zero-extending to 64 bits. */

.globl __atomic_load_1
.type __atomic_load_1,@function
__atomic_load_1:
	ldarb w0, [x0]
	ret

.globl __atomic_store_1
.type __atomic_store_1,@function
__atomic_store_1:
	stlrb w1, [x0]
	ret

.globl __atomic_exchange_1
.type __atomic_exchange_1,@function
__atomic_exchange_1:
1:	ldaxrb w2, [x0]
	stlxrb w3, w1, [x0]
	cbnz w3, 1b
	/* ldaxrb zero-extends, so w0 (low byte of w2) is the old value. */
	mov w0, w2
	ret

.globl __atomic_fetch_add_1
.type __atomic_fetch_add_1,@function
__atomic_fetch_add_1:
1:	ldaxrb w2, [x0]
	add w3, w2, w1
	stlxrb w4, w3, [x0]
	cbnz w4, 1b
	mov w0, w2
	ret

.globl __atomic_fetch_sub_1
.type __atomic_fetch_sub_1,@function
__atomic_fetch_sub_1:
1:	ldaxrb w2, [x0]
	sub w3, w2, w1
	stlxrb w4, w3, [x0]
	cbnz w4, 1b
	mov w0, w2
	ret

.globl __atomic_fetch_and_1
.type __atomic_fetch_and_1,@function
__atomic_fetch_and_1:
1:	ldaxrb w2, [x0]
	and w3, w2, w1
	stlxrb w4, w3, [x0]
	cbnz w4, 1b
	mov w0, w2
	ret

.globl __atomic_fetch_or_1
.type __atomic_fetch_or_1,@function
__atomic_fetch_or_1:
1:	ldaxrb w2, [x0]
	orr w3, w2, w1
	stlxrb w4, w3, [x0]
	cbnz w4, 1b
	mov w0, w2
	ret

.globl __atomic_fetch_xor_1
.type __atomic_fetch_xor_1,@function
__atomic_fetch_xor_1:
1:	ldaxrb w2, [x0]
	eor w3, w2, w1
	stlxrb w4, w3, [x0]
	cbnz w4, 1b
	mov w0, w2
	ret

.globl __atomic_compare_exchange_1
.type __atomic_compare_exchange_1,@function
__atomic_compare_exchange_1:
	ldrb w3, [x1]		/* expected */
1:	ldaxrb w4, [x0]
	cmp w4, w3
	b.ne 2f
	stlxrb w5, w2, [x0]
	cbnz w5, 1b
	mov w0, #1
	ret
2:	strb w4, [x1]		/* *expected = current */
	mov w0, #0
	ret

/* ===== 2-byte (halfword) ===== */

.globl __atomic_load_2
.type __atomic_load_2,@function
__atomic_load_2:
	ldarh w0, [x0]
	ret

.globl __atomic_store_2
.type __atomic_store_2,@function
__atomic_store_2:
	stlrh w1, [x0]
	ret

.globl __atomic_exchange_2
.type __atomic_exchange_2,@function
__atomic_exchange_2:
1:	ldaxrh w2, [x0]
	stlxrh w3, w1, [x0]
	cbnz w3, 1b
	mov w0, w2
	ret

.globl __atomic_fetch_add_2
.type __atomic_fetch_add_2,@function
__atomic_fetch_add_2:
1:	ldaxrh w2, [x0]
	add w3, w2, w1
	stlxrh w4, w3, [x0]
	cbnz w4, 1b
	mov w0, w2
	ret

.globl __atomic_fetch_sub_2
.type __atomic_fetch_sub_2,@function
__atomic_fetch_sub_2:
1:	ldaxrh w2, [x0]
	sub w3, w2, w1
	stlxrh w4, w3, [x0]
	cbnz w4, 1b
	mov w0, w2
	ret

.globl __atomic_fetch_and_2
.type __atomic_fetch_and_2,@function
__atomic_fetch_and_2:
1:	ldaxrh w2, [x0]
	and w3, w2, w1
	stlxrh w4, w3, [x0]
	cbnz w4, 1b
	mov w0, w2
	ret

.globl __atomic_fetch_or_2
.type __atomic_fetch_or_2,@function
__atomic_fetch_or_2:
1:	ldaxrh w2, [x0]
	orr w3, w2, w1
	stlxrh w4, w3, [x0]
	cbnz w4, 1b
	mov w0, w2
	ret

.globl __atomic_fetch_xor_2
.type __atomic_fetch_xor_2,@function
__atomic_fetch_xor_2:
1:	ldaxrh w2, [x0]
	eor w3, w2, w1
	stlxrh w4, w3, [x0]
	cbnz w4, 1b
	mov w0, w2
	ret

.globl __atomic_compare_exchange_2
.type __atomic_compare_exchange_2,@function
__atomic_compare_exchange_2:
	ldrh w3, [x1]
1:	ldaxrh w4, [x0]
	cmp w4, w3
	b.ne 2f
	stlxrh w5, w2, [x0]
	cbnz w5, 1b
	mov w0, #1
	ret
2:	strh w4, [x1]
	mov w0, #0
	ret

/* ===== 4-byte (word) =====
 * ldar/stlr/ldaxr/stlxr on W registers operate on 32 bits. */

.globl __atomic_load_4
.type __atomic_load_4,@function
__atomic_load_4:
	ldar w0, [x0]
	ret

.globl __atomic_store_4
.type __atomic_store_4,@function
__atomic_store_4:
	stlr w1, [x0]
	ret

.globl __atomic_exchange_4
.type __atomic_exchange_4,@function
__atomic_exchange_4:
1:	ldaxr w2, [x0]
	stlxr w3, w1, [x0]
	cbnz w3, 1b
	mov w0, w2
	ret

.globl __atomic_fetch_add_4
.type __atomic_fetch_add_4,@function
__atomic_fetch_add_4:
1:	ldaxr w2, [x0]
	add w3, w2, w1
	stlxr w4, w3, [x0]
	cbnz w4, 1b
	mov w0, w2
	ret

.globl __atomic_fetch_sub_4
.type __atomic_fetch_sub_4,@function
__atomic_fetch_sub_4:
1:	ldaxr w2, [x0]
	sub w3, w2, w1
	stlxr w4, w3, [x0]
	cbnz w4, 1b
	mov w0, w2
	ret

.globl __atomic_fetch_and_4
.type __atomic_fetch_and_4,@function
__atomic_fetch_and_4:
1:	ldaxr w2, [x0]
	and w3, w2, w1
	stlxr w4, w3, [x0]
	cbnz w4, 1b
	mov w0, w2
	ret

.globl __atomic_fetch_or_4
.type __atomic_fetch_or_4,@function
__atomic_fetch_or_4:
1:	ldaxr w2, [x0]
	orr w3, w2, w1
	stlxr w4, w3, [x0]
	cbnz w4, 1b
	mov w0, w2
	ret

.globl __atomic_fetch_xor_4
.type __atomic_fetch_xor_4,@function
__atomic_fetch_xor_4:
1:	ldaxr w2, [x0]
	eor w3, w2, w1
	stlxr w4, w3, [x0]
	cbnz w4, 1b
	mov w0, w2
	ret

.globl __atomic_compare_exchange_4
.type __atomic_compare_exchange_4,@function
__atomic_compare_exchange_4:
	ldr w3, [x1]
1:	ldaxr w4, [x0]
	cmp w4, w3
	b.ne 2f
	stlxr w5, w2, [x0]
	cbnz w5, 1b
	mov w0, #1
	ret
2:	str w4, [x1]
	mov w0, #0
	ret

/* ===== 8-byte (doubleword) =====
 * ldar/stlr/ldaxr/stlxr on X registers operate on 64 bits. */

.globl __atomic_load_8
.type __atomic_load_8,@function
__atomic_load_8:
	ldar x0, [x0]
	ret

.globl __atomic_store_8
.type __atomic_store_8,@function
__atomic_store_8:
	stlr x1, [x0]
	ret

.globl __atomic_exchange_8
.type __atomic_exchange_8,@function
__atomic_exchange_8:
1:	ldaxr x2, [x0]
	stlxr w3, x1, [x0]
	cbnz w3, 1b
	mov x0, x2
	ret

.globl __atomic_fetch_add_8
.type __atomic_fetch_add_8,@function
__atomic_fetch_add_8:
1:	ldaxr x2, [x0]
	add x3, x2, x1
	stlxr w4, x3, [x0]
	cbnz w4, 1b
	mov x0, x2
	ret

.globl __atomic_fetch_sub_8
.type __atomic_fetch_sub_8,@function
__atomic_fetch_sub_8:
1:	ldaxr x2, [x0]
	sub x3, x2, x1
	stlxr w4, x3, [x0]
	cbnz w4, 1b
	mov x0, x2
	ret

.globl __atomic_fetch_and_8
.type __atomic_fetch_and_8,@function
__atomic_fetch_and_8:
1:	ldaxr x2, [x0]
	and x3, x2, x1
	stlxr w4, x3, [x0]
	cbnz w4, 1b
	mov x0, x2
	ret

.globl __atomic_fetch_or_8
.type __atomic_fetch_or_8,@function
__atomic_fetch_or_8:
1:	ldaxr x2, [x0]
	orr x3, x2, x1
	stlxr w4, x3, [x0]
	cbnz w4, 1b
	mov x0, x2
	ret

.globl __atomic_fetch_xor_8
.type __atomic_fetch_xor_8,@function
__atomic_fetch_xor_8:
1:	ldaxr x2, [x0]
	eor x3, x2, x1
	stlxr w4, x3, [x0]
	cbnz w4, 1b
	mov x0, x2
	ret

.globl __atomic_compare_exchange_8
.type __atomic_compare_exchange_8,@function
__atomic_compare_exchange_8:
	ldr x3, [x1]
1:	ldaxr x4, [x0]
	cmp x4, x3
	b.ne 2f
	stlxr w5, x2, [x0]
	cbnz w5, 1b
	mov w0, #1
	ret
2:	str x4, [x1]
	mov w0, #0
	ret

.section .note.GNU-stack,"",@progbits
