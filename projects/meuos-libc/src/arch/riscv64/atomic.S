.text

/* MeuOS compiler atomic runtime, RISC-V 64 LP64D.
 *
 * Provides the __atomic_* compiler-runtime ABI that mcc lowers C11 atomics
 * into.  RISC-V's A extension gives us:
 *   - lr.w/lr.d with .aqrl: load-reserved (acquire+release) for the RMW loop
 *   - sc.w/sc.d with .aqrl: store-conditional (acquire+release)
 *   - fence: full barrier for atomic_thread_fence
 * Sub-word (1/2-byte) atomics use word-level lr.w/sc.w with masking because
 * the A extension only offers 32/64-bit load-reserved.
 *
 * Symbol set matches src/arch/aarch64/atomic.S so the rest of libc and
 * applications link identically across architectures. */

.globl atomic_thread_fence
.type atomic_thread_fence,@function
atomic_thread_fence:
	fence rw, rw
	ret

.globl atomic_signal_fence
.type atomic_signal_fence,@function
atomic_signal_fence:
	ret

/* ===== 1-byte (byte) ===== */

.globl __atomic_load_1
.type __atomic_load_1,@function
__atomic_load_1:
	lbu a0, 0(a0)
	fence r, rw
	ret

.globl __atomic_store_1
.type __atomic_store_1,@function
__atomic_store_1:
	fence rw, w
	sb a1, 0(a0)
	ret

.globl __atomic_exchange_1
.type __atomic_exchange_1,@function
__atomic_exchange_1:
	andi a2, a0, 3
	slli a2, a2, 3		/* byte shift */
	li a3, 0xff
	sll a3, a3, a2		/* mask */
	andi a4, a1, 0xff
	sll a4, a4, a2		/* newval in position */
	and a5, a0, -4		/* word-aligned address */
1:	lr.w.aqrl a6, 0(a5)
	not a7, a3
	and a7, a6, a7
	or a7, a7, a4
	sc.w.aqrl x28, a7, 0(a5)
	bnez x28, 1b
	and a0, a6, a3
	srl a0, a0, a2		/* old byte value */
	ret

.globl __atomic_fetch_add_1
.type __atomic_fetch_add_1,@function
__atomic_fetch_add_1:
	andi a1, a1, 0xff
	andi a2, a0, 3
	slli a2, a2, 3
	li a3, 0xff
	sll a3, a3, a2
	and a5, a0, -4
1:	lr.w.aqrl a6, 0(a5)
	and a0, a6, a3
	srl a0, a0, a2		/* old byte value (return) */
	sll a4, a0, a2
	add a4, a4, a1
	and a4, a4, a3
	not a7, a3
	and a6, a6, a7
	or a7, a6, a4
	sc.w.aqrl x28, a7, 0(a5)
	bnez x28, 1b
	ret

.globl __atomic_fetch_sub_1
.type __atomic_fetch_sub_1,@function
__atomic_fetch_sub_1:
	andi a1, a1, 0xff
	andi a2, a0, 3
	slli a2, a2, 3
	li a3, 0xff
	sll a3, a3, a2
	and a5, a0, -4
1:	lr.w.aqrl a6, 0(a5)
	and a0, a6, a3
	srl a0, a0, a2
	sll a4, a0, a2
	sub a4, a4, a1
	and a4, a4, a3
	not a7, a3
	and a6, a6, a7
	or a7, a6, a4
	sc.w.aqrl x28, a7, 0(a5)
	bnez x28, 1b
	ret

.globl __atomic_fetch_and_1
.type __atomic_fetch_and_1,@function
__atomic_fetch_and_1:
	andi a1, a1, 0xff
	andi a2, a0, 3
	slli a2, a2, 3
	li a3, 0xff
	sll a3, a3, a2
	and a5, a0, -4
1:	lr.w.aqrl a6, 0(a5)
	and a0, a6, a3
	srl a0, a0, a2
	and a0, a0, a1
	sll a4, a0, a2
	and a4, a4, a3
	not a7, a3
	and a6, a6, a7
	or a7, a6, a4
	sc.w.aqrl x28, a7, 0(a5)
	bnez x28, 1b
	ret

.globl __atomic_fetch_or_1
.type __atomic_fetch_or_1,@function
__atomic_fetch_or_1:
	andi a1, a1, 0xff
	andi a2, a0, 3
	slli a2, a2, 3
	li a3, 0xff
	sll a3, a3, a2
	and a5, a0, -4
1:	lr.w.aqrl a6, 0(a5)
	and a0, a6, a3
	srl a0, a0, a2
	or a0, a0, a1
	sll a4, a0, a2
	and a4, a4, a3
	not a7, a3
	and a6, a6, a7
	or a7, a6, a4
	sc.w.aqrl x28, a7, 0(a5)
	bnez x28, 1b
	ret

.globl __atomic_fetch_xor_1
.type __atomic_fetch_xor_1,@function
__atomic_fetch_xor_1:
	andi a1, a1, 0xff
	andi a2, a0, 3
	slli a2, a2, 3
	li a3, 0xff
	sll a3, a3, a2
	and a5, a0, -4
1:	lr.w.aqrl a6, 0(a5)
	and a0, a6, a3
	srl a0, a0, a2
	xor a0, a0, a1
	sll a4, a0, a2
	and a4, a4, a3
	not a7, a3
	and a6, a6, a7
	or a7, a6, a4
	sc.w.aqrl x28, a7, 0(a5)
	bnez x28, 1b
	ret

.globl __atomic_compare_exchange_1
.type __atomic_compare_exchange_1,@function
__atomic_compare_exchange_1:
	andi a3, a0, 3
	slli a3, a3, 3		/* shift in a3 */
	li a4, 0xff
	sll a4, a4, a3		/* mask in a4 */
	andi a6, a2, 0xff
	sll a6, a6, a3		/* desired (masked, in position) in a6 */
	and a5, a0, -4		/* word-aligned address in a5 */
	lbu a7, 0(a1)
	sll a7, a7, a3		/* expected (in position) in a7 */
1:	lr.w.aqrl a2, 0(a5)	/* current word */
	and a0, a2, a4		/* current byte (masked) */
	bne a0, a7, 2f
	not a7, a4		/* ~mask */
	and a7, a2, a7		/* word with target byte zeroed */
	or a6, a7, a6		/* insert desired byte */
	sc.w.aqrl x28, a6, 0(a5)	/* store corrected word */
	bnez x28, 1b
	li a0, 1
	ret
2:	srl a0, a0, a3		/* current byte value */
	sb a0, 0(a1)		/* *expected = current */
	li a0, 0
	ret

/* ===== 2-byte (halfword) ===== */

.globl __atomic_load_2
.type __atomic_load_2,@function
__atomic_load_2:
	lhu a0, 0(a0)
	fence r, rw
	ret

.globl __atomic_store_2
.type __atomic_store_2,@function
__atomic_store_2:
	fence rw, w
	sh a1, 0(a0)
	ret

.globl __atomic_exchange_2
.type __atomic_exchange_2,@function
__atomic_exchange_2:
	andi a2, a0, 2
	slli a2, a2, 3
	li a3, 0xffff
	sll a3, a3, a2
	slli a4, a1, 48
	srli a4, a4, 48
	sll a4, a4, a2
	and a5, a0, -4
1:	lr.w.aqrl a6, 0(a5)
	not a7, a3
	and a7, a6, a7
	or a7, a7, a4
	sc.w.aqrl x28, a7, 0(a5)
	bnez x28, 1b
	and a0, a6, a3
	srl a0, a0, a2
	ret

.globl __atomic_fetch_add_2
.type __atomic_fetch_add_2,@function
__atomic_fetch_add_2:
	slli a1, a1, 48
	srli a1, a1, 48
	andi a2, a0, 2
	slli a2, a2, 3
	li a3, 0xffff
	sll a3, a3, a2
	and a5, a0, -4
1:	lr.w.aqrl a6, 0(a5)
	and a0, a6, a3
	srl a0, a0, a2
	sll a4, a0, a2
	add a4, a4, a1
	and a4, a4, a3
	not a7, a3
	and a6, a6, a7
	or a7, a6, a4
	sc.w.aqrl x28, a7, 0(a5)
	bnez x28, 1b
	ret

.globl __atomic_fetch_sub_2
.type __atomic_fetch_sub_2,@function
__atomic_fetch_sub_2:
	slli a1, a1, 48
	srli a1, a1, 48
	andi a2, a0, 2
	slli a2, a2, 3
	li a3, 0xffff
	sll a3, a3, a2
	and a5, a0, -4
1:	lr.w.aqrl a6, 0(a5)
	and a0, a6, a3
	srl a0, a0, a2
	sll a4, a0, a2
	sub a4, a4, a1
	and a4, a4, a3
	not a7, a3
	and a6, a6, a7
	or a7, a6, a4
	sc.w.aqrl x28, a7, 0(a5)
	bnez x28, 1b
	ret

.globl __atomic_fetch_and_2
.type __atomic_fetch_and_2,@function
__atomic_fetch_and_2:
	slli a1, a1, 48
	srli a1, a1, 48
	andi a2, a0, 2
	slli a2, a2, 3
	li a3, 0xffff
	sll a3, a3, a2
	and a5, a0, -4
1:	lr.w.aqrl a6, 0(a5)
	and a0, a6, a3
	srl a0, a0, a2
	and a0, a0, a1
	sll a4, a0, a2
	and a4, a4, a3
	not a7, a3
	and a6, a6, a7
	or a7, a6, a4
	sc.w.aqrl x28, a7, 0(a5)
	bnez x28, 1b
	ret

.globl __atomic_fetch_or_2
.type __atomic_fetch_or_2,@function
__atomic_fetch_or_2:
	slli a1, a1, 48
	srli a1, a1, 48
	andi a2, a0, 2
	slli a2, a2, 3
	li a3, 0xffff
	sll a3, a3, a2
	and a5, a0, -4
1:	lr.w.aqrl a6, 0(a5)
	and a0, a6, a3
	srl a0, a0, a2
	or a0, a0, a1
	sll a4, a0, a2
	and a4, a4, a3
	not a7, a3
	and a6, a6, a7
	or a7, a6, a4
	sc.w.aqrl x28, a7, 0(a5)
	bnez x28, 1b
	ret

.globl __atomic_fetch_xor_2
.type __atomic_fetch_xor_2,@function
__atomic_fetch_xor_2:
	slli a1, a1, 48
	srli a1, a1, 48
	andi a2, a0, 2
	slli a2, a2, 3
	li a3, 0xffff
	sll a3, a3, a2
	and a5, a0, -4
1:	lr.w.aqrl a6, 0(a5)
	and a0, a6, a3
	srl a0, a0, a2
	xor a0, a0, a1
	sll a4, a0, a2
	and a4, a4, a3
	not a7, a3
	and a6, a6, a7
	or a7, a6, a4
	sc.w.aqrl x28, a7, 0(a5)
	bnez x28, 1b
	ret

.globl __atomic_compare_exchange_2
.type __atomic_compare_exchange_2,@function
__atomic_compare_exchange_2:
	andi a3, a0, 2
	slli a3, a3, 3		/* shift in a3 */
	li a4, 0xffff
	sll a4, a4, a3		/* mask in a4 */
	slli a6, a2, 48
	srli a6, a6, 48
	sll a6, a6, a3		/* desired (masked, in position) in a6 */
	and a5, a0, -4		/* word-aligned address in a5 */
	lhu a7, 0(a1)
	sll a7, a7, a3		/* expected (in position) in a7 */
1:	lr.w.aqrl a2, 0(a5)	/* current word */
	and a0, a2, a4		/* current halfword (masked) */
	bne a0, a7, 2f
	not a7, a4		/* ~mask */
	and a7, a2, a7		/* word with target halfword zeroed */
	or a6, a7, a6		/* insert desired halfword */
	sc.w.aqrl x28, a6, 0(a5)	/* store corrected word */
	bnez x28, 1b
	li a0, 1
	ret
2:	srl a0, a0, a3		/* current halfword value */
	sh a0, 0(a1)		/* *expected = current */
	li a0, 0
	ret

/* ===== 4-byte (word) ===== */

.globl __atomic_load_4
.type __atomic_load_4,@function
__atomic_load_4:
	lw a0, 0(a0)
	fence r, rw
	ret

.globl __atomic_store_4
.type __atomic_store_4,@function
__atomic_store_4:
	fence rw, w
	sw a1, 0(a0)
	ret

.globl __atomic_exchange_4
.type __atomic_exchange_4,@function
__atomic_exchange_4:
1:	lr.w.aqrl a2, 0(a0)
	sc.w.aqrl a3, a1, 0(a0)
	bnez a3, 1b
	mv a0, a2
	ret

.globl __atomic_fetch_add_4
.type __atomic_fetch_add_4,@function
__atomic_fetch_add_4:
1:	lr.w.aqrl a2, 0(a0)
	add a4, a2, a1
	sc.w.aqrl a3, a4, 0(a0)
	bnez a3, 1b
	mv a0, a2
	ret

.globl __atomic_fetch_sub_4
.type __atomic_fetch_sub_4,@function
__atomic_fetch_sub_4:
1:	lr.w.aqrl a2, 0(a0)
	sub a4, a2, a1
	sc.w.aqrl a3, a4, 0(a0)
	bnez a3, 1b
	mv a0, a2
	ret

.globl __atomic_fetch_and_4
.type __atomic_fetch_and_4,@function
__atomic_fetch_and_4:
1:	lr.w.aqrl a2, 0(a0)
	and a4, a2, a1
	sc.w.aqrl a3, a4, 0(a0)
	bnez a3, 1b
	mv a0, a2
	ret

.globl __atomic_fetch_or_4
.type __atomic_fetch_or_4,@function
__atomic_fetch_or_4:
1:	lr.w.aqrl a2, 0(a0)
	or a4, a2, a1
	sc.w.aqrl a3, a4, 0(a0)
	bnez a3, 1b
	mv a0, a2
	ret

.globl __atomic_fetch_xor_4
.type __atomic_fetch_xor_4,@function
__atomic_fetch_xor_4:
1:	lr.w.aqrl a2, 0(a0)
	xor a4, a2, a1
	sc.w.aqrl a3, a4, 0(a0)
	bnez a3, 1b
	mv a0, a2
	ret

.globl __atomic_compare_exchange_4
.type __atomic_compare_exchange_4,@function
__atomic_compare_exchange_4:
	lw a3, 0(a1)		/* expected */
1:	lr.w.aqrl a4, 0(a0)
	bne a4, a3, 2f
	sc.w.aqrl a5, a2, 0(a0)
	bnez a5, 1b
	li a0, 1
	ret
2:	sw a4, 0(a1)		/* *expected = current */
	li a0, 0
	ret

/* ===== 8-byte (doubleword) ===== */

.globl __atomic_load_8
.type __atomic_load_8,@function
__atomic_load_8:
	ld a0, 0(a0)
	fence r, rw
	ret

.globl __atomic_store_8
.type __atomic_store_8,@function
__atomic_store_8:
	fence rw, w
	sd a1, 0(a0)
	ret

.globl __atomic_exchange_8
.type __atomic_exchange_8,@function
__atomic_exchange_8:
1:	lr.d.aqrl a2, 0(a0)
	sc.d.aqrl a3, a1, 0(a0)
	bnez a3, 1b
	mv a0, a2
	ret

.globl __atomic_fetch_add_8
.type __atomic_fetch_add_8,@function
__atomic_fetch_add_8:
1:	lr.d.aqrl a2, 0(a0)
	add a4, a2, a1
	sc.d.aqrl a3, a4, 0(a0)
	bnez a3, 1b
	mv a0, a2
	ret

.globl __atomic_fetch_sub_8
.type __atomic_fetch_sub_8,@function
__atomic_fetch_sub_8:
1:	lr.d.aqrl a2, 0(a0)
	sub a4, a2, a1
	sc.d.aqrl a3, a4, 0(a0)
	bnez a3, 1b
	mv a0, a2
	ret

.globl __atomic_fetch_and_8
.type __atomic_fetch_and_8,@function
__atomic_fetch_and_8:
1:	lr.d.aqrl a2, 0(a0)
	and a4, a2, a1
	sc.d.aqrl a3, a4, 0(a0)
	bnez a3, 1b
	mv a0, a2
	ret

.globl __atomic_fetch_or_8
.type __atomic_fetch_or_8,@function
__atomic_fetch_or_8:
1:	lr.d.aqrl a2, 0(a0)
	or a4, a2, a1
	sc.d.aqrl a3, a4, 0(a0)
	bnez a3, 1b
	mv a0, a2
	ret

.globl __atomic_fetch_xor_8
.type __atomic_fetch_xor_8,@function
__atomic_fetch_xor_8:
1:	lr.d.aqrl a2, 0(a0)
	xor a4, a2, a1
	sc.d.aqrl a3, a4, 0(a0)
	bnez a3, 1b
	mv a0, a2
	ret

.globl __atomic_compare_exchange_8
.type __atomic_compare_exchange_8,@function
__atomic_compare_exchange_8:
	ld a3, 0(a1)		/* expected */
1:	lr.d.aqrl a4, 0(a0)
	bne a4, a3, 2f
	sc.d.aqrl a5, a2, 0(a0)
	bnez a5, 1b
	li a0, 1
	ret
2:	sd a4, 0(a1)		/* *expected = current */
	li a0, 0
	ret

.section .note.GNU-stack,"",@progbits
