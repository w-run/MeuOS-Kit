.text

/* MeuOS compiler atomic runtime, LoongArch64 LP64D.
 *
 * Provides the __atomic_* compiler-runtime ABI that mcc lowers C11 atomics
 * into.  LoongArch's LL/SC instructions:
 *   - ll.w / ll.d: load-linked (word/doubleword)
 *   - sc.w / sc.d: store-conditional (same register holds value AND success)
 *   - dbar 0:       full memory barrier
 * Sub-word (1/2-byte) atomics use word-level ll.w/sc.w with masking because
 * LoongArch only offers 32/64-bit load-linked.
 *
 * Symbol set matches src/arch/aarch64/atomic.S so the rest of libc and
 * applications link identically across architectures. */

.globl atomic_thread_fence
.type atomic_thread_fence,@function
atomic_thread_fence:
	dbar 0
	jr $ra

.globl atomic_signal_fence
.type atomic_signal_fence,@function
atomic_signal_fence:
	jr $ra

/* ===== 1-byte (byte) ===== */

.globl __atomic_load_1
.type __atomic_load_1,@function
__atomic_load_1:
	ld.bu $a0, $a0, 0
	dbar 0
	jr $ra

.globl __atomic_store_1
.type __atomic_store_1,@function
__atomic_store_1:
	dbar 0
	st.b $a1, $a0, 0
	jr $ra

.globl __atomic_exchange_1
.type __atomic_exchange_1,@function
__atomic_exchange_1:
	andi $t0, $a0, 3
	slli.d $t0, $t0, 3		/* byte shift */
	li.w $t1, 0xff
	sll.d $t1, $t1, $t0		/* mask */
	andi $t2, $a1, 0xff
	sll.d $t2, $t2, $t0		/* newval in position */
	or $t3, $a0, $zero
	bstrins.d $t3, $zero, 1, 0	/* word-aligned address */
1:
	ll.w $t4, $t3, 0		/* current word */
	nor $t5, $t1, $zero		/* ~mask */
	and $t5, $t4, $t5		/* clear old byte */
	or $t5, $t5, $t2		/* set new byte */
	sc.w $t5, $t3, 0		/* $t5 = success */
	beqz $t5, 1b
	and $a0, $t4, $t1		/* old byte value (masked) */
	srl.d $a0, $a0, $t0		/* extract */
	jr $ra

.globl __atomic_fetch_add_1
.type __atomic_fetch_add_1,@function
__atomic_fetch_add_1:
	andi $t0, $a0, 3
	slli.d $t0, $t0, 3
	li.w $t1, 0xff
	sll.d $t1, $t1, $t0		/* mask */
	andi $t2, $a1, 0xff		/* increment */
	or $t3, $a0, $zero
	bstrins.d $t3, $zero, 1, 0
1:
	ll.w $t4, $t3, 0		/* current word */
	and $a0, $t4, $t1		/* old byte masked */
	srl.d $a0, $a0, $t0		/* old byte value (return) */
	sll.d $t5, $a0, $t0		/* old byte in position */
	add.d $t5, $t5, $t2		/* add increment */
	and $t5, $t5, $t1		/* wrap (mask) */
	nor $t6, $t1, $zero		/* ~mask */
	and $t6, $t4, $t6		/* clear old byte */
	or $t5, $t6, $t5		/* new word */
	sc.w $t5, $t3, 0		/* $t5 = success */
	beqz $t5, 1b
	jr $ra

.globl __atomic_fetch_sub_1
.type __atomic_fetch_sub_1,@function
__atomic_fetch_sub_1:
	andi $t0, $a0, 3
	slli.d $t0, $t0, 3
	li.w $t1, 0xff
	sll.d $t1, $t1, $t0
	andi $t2, $a1, 0xff
	or $t3, $a0, $zero
	bstrins.d $t3, $zero, 1, 0
1:
	ll.w $t4, $t3, 0
	and $a0, $t4, $t1
	srl.d $a0, $a0, $t0
	sll.d $t5, $a0, $t0
	sub.d $t5, $t5, $t2
	and $t5, $t5, $t1
	nor $t6, $t1, $zero
	and $t6, $t4, $t6
	or $t5, $t6, $t5
	sc.w $t5, $t3, 0
	beqz $t5, 1b
	jr $ra

.globl __atomic_fetch_and_1
.type __atomic_fetch_and_1,@function
__atomic_fetch_and_1:
	andi $t0, $a0, 3
	slli.d $t0, $t0, 3
	li.w $t1, 0xff
	sll.d $t1, $t1, $t0
	andi $t2, $a1, 0xff
	or $t3, $a0, $zero
	bstrins.d $t3, $zero, 1, 0
1:
	ll.w $t4, $t3, 0
	and $a0, $t4, $t1
	srl.d $a0, $a0, $t0
	and $a0, $a0, $t2		/* result = old & op */
	sll.d $t5, $a0, $t0
	and $t5, $t5, $t1
	nor $t6, $t1, $zero
	and $t6, $t4, $t6
	or $t5, $t6, $t5
	sc.w $t5, $t3, 0
	beqz $t5, 1b
	jr $ra

.globl __atomic_fetch_or_1
.type __atomic_fetch_or_1,@function
__atomic_fetch_or_1:
	andi $t0, $a0, 3
	slli.d $t0, $t0, 3
	li.w $t1, 0xff
	sll.d $t1, $t1, $t0
	andi $t2, $a1, 0xff
	or $t3, $a0, $zero
	bstrins.d $t3, $zero, 1, 0
1:
	ll.w $t4, $t3, 0
	and $a0, $t4, $t1
	srl.d $a0, $a0, $t0
	or $a0, $a0, $t2
	sll.d $t5, $a0, $t0
	and $t5, $t5, $t1
	nor $t6, $t1, $zero
	and $t6, $t4, $t6
	or $t5, $t6, $t5
	sc.w $t5, $t3, 0
	beqz $t5, 1b
	jr $ra

.globl __atomic_fetch_xor_1
.type __atomic_fetch_xor_1,@function
__atomic_fetch_xor_1:
	andi $t0, $a0, 3
	slli.d $t0, $t0, 3
	li.w $t1, 0xff
	sll.d $t1, $t1, $t0
	andi $t2, $a1, 0xff
	or $t3, $a0, $zero
	bstrins.d $t3, $zero, 1, 0
1:
	ll.w $t4, $t3, 0
	and $a0, $t4, $t1
	srl.d $a0, $a0, $t0
	xor $a0, $a0, $t2
	sll.d $t5, $a0, $t0
	and $t5, $t5, $t1
	nor $t6, $t1, $zero
	and $t6, $t4, $t6
	or $t5, $t6, $t5
	sc.w $t5, $t3, 0
	beqz $t5, 1b
	jr $ra

.globl __atomic_compare_exchange_1
.type __atomic_compare_exchange_1,@function
__atomic_compare_exchange_1:
	andi $t0, $a0, 3
	slli.d $t0, $t0, 3		/* shift */
	li.w $t1, 0xff
	sll.d $t1, $t1, $t0		/* mask */
	andi $t2, $a2, 0xff
	sll.d $t2, $t2, $t0		/* desired in position */
	or $t3, $a0, $zero
	bstrins.d $t3, $zero, 1, 0	/* word-aligned address */
	ld.bu $t4, $a1, 0			/* expected byte */
	sll.d $t4, $t4, $t0		/* expected in position */
1:
	ll.w $t5, $t3, 0		/* current word */
	and $t6, $t5, $t1		/* current byte masked */
	bne $t6, $t4, 2f
	nor $t7, $t1, $zero
	and $t7, $t5, $t7		/* clear old byte */
	or $t7, $t7, $t2		/* combined word */
	sc.w $t7, $t3, 0		/* $t7 = success */
	beqz $t7, 1b
	li.w $a0, 1
	jr $ra
2:
	srl.d $t6, $t6, $t0		/* current byte value */
	st.b $t6, $a1, 0		/* *expected = current */
	li.w $a0, 0
	jr $ra

/* ===== 2-byte (halfword) ===== */

.globl __atomic_load_2
.type __atomic_load_2,@function
__atomic_load_2:
	ld.hu $a0, $a0, 0
	dbar 0
	jr $ra

.globl __atomic_store_2
.type __atomic_store_2,@function
__atomic_store_2:
	dbar 0
	st.h $a1, $a0, 0
	jr $ra

.globl __atomic_exchange_2
.type __atomic_exchange_2,@function
__atomic_exchange_2:
	andi $t0, $a0, 2
	slli.d $t0, $t0, 3		/* halfword shift: 0 or 16 */
	li.w $t1, 0xffff
	sll.d $t1, $t1, $t0		/* mask */
	andi $t2, $a1, 0xffff
	sll.d $t2, $t2, $t0		/* newval in position */
	or $t3, $a0, $zero
	bstrins.d $t3, $zero, 1, 0	/* word-aligned address */
1:
	ll.w $t4, $t3, 0
	nor $t5, $t1, $zero
	and $t5, $t4, $t5
	or $t5, $t5, $t2
	sc.w $t5, $t3, 0
	beqz $t5, 1b
	and $a0, $t4, $t1
	srl.d $a0, $a0, $t0
	jr $ra

.globl __atomic_fetch_add_2
.type __atomic_fetch_add_2,@function
__atomic_fetch_add_2:
	andi $t0, $a0, 2
	slli.d $t0, $t0, 3
	li.w $t1, 0xffff
	sll.d $t1, $t1, $t0
	andi $t2, $a1, 0xffff
	or $t3, $a0, $zero
	bstrins.d $t3, $zero, 1, 0
1:
	ll.w $t4, $t3, 0
	and $a0, $t4, $t1
	srl.d $a0, $a0, $t0
	sll.d $t5, $a0, $t0
	add.d $t5, $t5, $t2
	and $t5, $t5, $t1
	nor $t6, $t1, $zero
	and $t6, $t4, $t6
	or $t5, $t6, $t5
	sc.w $t5, $t3, 0
	beqz $t5, 1b
	jr $ra

.globl __atomic_fetch_sub_2
.type __atomic_fetch_sub_2,@function
__atomic_fetch_sub_2:
	andi $t0, $a0, 2
	slli.d $t0, $t0, 3
	li.w $t1, 0xffff
	sll.d $t1, $t1, $t0
	andi $t2, $a1, 0xffff
	or $t3, $a0, $zero
	bstrins.d $t3, $zero, 1, 0
1:
	ll.w $t4, $t3, 0
	and $a0, $t4, $t1
	srl.d $a0, $a0, $t0
	sll.d $t5, $a0, $t0
	sub.d $t5, $t5, $t2
	and $t5, $t5, $t1
	nor $t6, $t1, $zero
	and $t6, $t4, $t6
	or $t5, $t6, $t5
	sc.w $t5, $t3, 0
	beqz $t5, 1b
	jr $ra

.globl __atomic_fetch_and_2
.type __atomic_fetch_and_2,@function
__atomic_fetch_and_2:
	andi $t0, $a0, 2
	slli.d $t0, $t0, 3
	li.w $t1, 0xffff
	sll.d $t1, $t1, $t0
	andi $t2, $a1, 0xffff
	or $t3, $a0, $zero
	bstrins.d $t3, $zero, 1, 0
1:
	ll.w $t4, $t3, 0
	and $a0, $t4, $t1
	srl.d $a0, $a0, $t0
	and $a0, $a0, $t2
	sll.d $t5, $a0, $t0
	and $t5, $t5, $t1
	nor $t6, $t1, $zero
	and $t6, $t4, $t6
	or $t5, $t6, $t5
	sc.w $t5, $t3, 0
	beqz $t5, 1b
	jr $ra

.globl __atomic_fetch_or_2
.type __atomic_fetch_or_2,@function
__atomic_fetch_or_2:
	andi $t0, $a0, 2
	slli.d $t0, $t0, 3
	li.w $t1, 0xffff
	sll.d $t1, $t1, $t0
	andi $t2, $a1, 0xffff
	or $t3, $a0, $zero
	bstrins.d $t3, $zero, 1, 0
1:
	ll.w $t4, $t3, 0
	and $a0, $t4, $t1
	srl.d $a0, $a0, $t0
	or $a0, $a0, $t2
	sll.d $t5, $a0, $t0
	and $t5, $t5, $t1
	nor $t6, $t1, $zero
	and $t6, $t4, $t6
	or $t5, $t6, $t5
	sc.w $t5, $t3, 0
	beqz $t5, 1b
	jr $ra

.globl __atomic_fetch_xor_2
.type __atomic_fetch_xor_2,@function
__atomic_fetch_xor_2:
	andi $t0, $a0, 2
	slli.d $t0, $t0, 3
	li.w $t1, 0xffff
	sll.d $t1, $t1, $t0
	andi $t2, $a1, 0xffff
	or $t3, $a0, $zero
	bstrins.d $t3, $zero, 1, 0
1:
	ll.w $t4, $t3, 0
	and $a0, $t4, $t1
	srl.d $a0, $a0, $t0
	xor $a0, $a0, $t2
	sll.d $t5, $a0, $t0
	and $t5, $t5, $t1
	nor $t6, $t1, $zero
	and $t6, $t4, $t6
	or $t5, $t6, $t5
	sc.w $t5, $t3, 0
	beqz $t5, 1b
	jr $ra

.globl __atomic_compare_exchange_2
.type __atomic_compare_exchange_2,@function
__atomic_compare_exchange_2:
	andi $t0, $a0, 2
	slli.d $t0, $t0, 3
	li.w $t1, 0xffff
	sll.d $t1, $t1, $t0
	andi $t2, $a2, 0xffff
	sll.d $t2, $t2, $t0
	or $t3, $a0, $zero
	bstrins.d $t3, $zero, 1, 0
	ld.hu $t4, $a1, 0
	sll.d $t4, $t4, $t0
1:
	ll.w $t5, $t3, 0
	and $t6, $t5, $t1
	bne $t6, $t4, 2f
	nor $t7, $t1, $zero
	and $t7, $t5, $t7
	or $t7, $t7, $t2
	sc.w $t7, $t3, 0
	beqz $t7, 1b
	li.w $a0, 1
	jr $ra
2:
	srl.d $t6, $t6, $t0
	st.h $t6, $a1, 0
	li.w $a0, 0
	jr $ra

/* ===== 4-byte (word) ===== */

.globl __atomic_load_4
.type __atomic_load_4,@function
__atomic_load_4:
	ld.w $a0, $a0, 0
	dbar 0
	jr $ra

.globl __atomic_store_4
.type __atomic_store_4,@function
__atomic_store_4:
	dbar 0
	st.w $a1, $a0, 0
	jr $ra

.globl __atomic_exchange_4
.type __atomic_exchange_4,@function
__atomic_exchange_4:
1:
	ll.w $a2, $a0, 0		/* current */
	or $a3, $a1, $zero		/* value to store */
	sc.w $a3, $a0, 0		/* $a3 = success */
	beqz $a3, 1b
	move $a0, $a2			/* return old */
	jr $ra

.globl __atomic_fetch_add_4
.type __atomic_fetch_add_4,@function
__atomic_fetch_add_4:
1:
	ll.w $a2, $a0, 0
	add.d $a3, $a2, $a1
	sc.w $a3, $a0, 0
	beqz $a3, 1b
	move $a0, $a2
	jr $ra

.globl __atomic_fetch_sub_4
.type __atomic_fetch_sub_4,@function
__atomic_fetch_sub_4:
1:
	ll.w $a2, $a0, 0
	sub.d $a3, $a2, $a1
	sc.w $a3, $a0, 0
	beqz $a3, 1b
	move $a0, $a2
	jr $ra

.globl __atomic_fetch_and_4
.type __atomic_fetch_and_4,@function
__atomic_fetch_and_4:
1:
	ll.w $a2, $a0, 0
	and $a3, $a2, $a1
	sc.w $a3, $a0, 0
	beqz $a3, 1b
	move $a0, $a2
	jr $ra

.globl __atomic_fetch_or_4
.type __atomic_fetch_or_4,@function
__atomic_fetch_or_4:
1:
	ll.w $a2, $a0, 0
	or $a3, $a2, $a1
	sc.w $a3, $a0, 0
	beqz $a3, 1b
	move $a0, $a2
	jr $ra

.globl __atomic_fetch_xor_4
.type __atomic_fetch_xor_4,@function
__atomic_fetch_xor_4:
1:
	ll.w $a2, $a0, 0
	xor $a3, $a2, $a1
	sc.w $a3, $a0, 0
	beqz $a3, 1b
	move $a0, $a2
	jr $ra

.globl __atomic_compare_exchange_4
.type __atomic_compare_exchange_4,@function
__atomic_compare_exchange_4:
	ld.w $a3, $a1, 0		/* expected (32-bit, sign-extended) */
1:
	ll.w $a4, $a0, 0		/* current (32-bit, sign-extended) */
	bne $a4, $a3, 2f
	or $a5, $a2, $zero		/* desired */
	sc.w $a5, $a0, 0		/* $a5 = success */
	beqz $a5, 1b
	li.w $a0, 1
	jr $ra
2:
	st.w $a4, $a1, 0		/* *expected = current */
	li.w $a0, 0
	jr $ra

/* ===== 8-byte (doubleword) ===== */

.globl __atomic_load_8
.type __atomic_load_8,@function
__atomic_load_8:
	ld.d $a0, $a0, 0
	dbar 0
	jr $ra

.globl __atomic_store_8
.type __atomic_store_8,@function
__atomic_store_8:
	dbar 0
	st.d $a1, $a0, 0
	jr $ra

.globl __atomic_exchange_8
.type __atomic_exchange_8,@function
__atomic_exchange_8:
1:
	ll.d $a2, $a0, 0
	or $a3, $a1, $zero
	sc.d $a3, $a0, 0
	beqz $a3, 1b
	move $a0, $a2
	jr $ra

.globl __atomic_fetch_add_8
.type __atomic_fetch_add_8,@function
__atomic_fetch_add_8:
1:
	ll.d $a2, $a0, 0
	add.d $a3, $a2, $a1
	sc.d $a3, $a0, 0
	beqz $a3, 1b
	move $a0, $a2
	jr $ra

.globl __atomic_fetch_sub_8
.type __atomic_fetch_sub_8,@function
__atomic_fetch_sub_8:
1:
	ll.d $a2, $a0, 0
	sub.d $a3, $a2, $a1
	sc.d $a3, $a0, 0
	beqz $a3, 1b
	move $a0, $a2
	jr $ra

.globl __atomic_fetch_and_8
.type __atomic_fetch_and_8,@function
__atomic_fetch_and_8:
1:
	ll.d $a2, $a0, 0
	and $a3, $a2, $a1
	sc.d $a3, $a0, 0
	beqz $a3, 1b
	move $a0, $a2
	jr $ra

.globl __atomic_fetch_or_8
.type __atomic_fetch_or_8,@function
__atomic_fetch_or_8:
1:
	ll.d $a2, $a0, 0
	or $a3, $a2, $a1
	sc.d $a3, $a0, 0
	beqz $a3, 1b
	move $a0, $a2
	jr $ra

.globl __atomic_fetch_xor_8
.type __atomic_fetch_xor_8,@function
__atomic_fetch_xor_8:
1:
	ll.d $a2, $a0, 0
	xor $a3, $a2, $a1
	sc.d $a3, $a0, 0
	beqz $a3, 1b
	move $a0, $a2
	jr $ra

.globl __atomic_compare_exchange_8
.type __atomic_compare_exchange_8,@function
__atomic_compare_exchange_8:
	ld.d $a3, $a1, 0		/* expected */
1:
	ll.d $a4, $a0, 0		/* current */
	bne $a4, $a3, 2f
	or $a5, $a2, $zero		/* desired */
	sc.d $a5, $a0, 0		/* $a5 = success */
	beqz $a5, 1b
	li.w $a0, 1
	jr $ra
2:
	st.d $a4, $a1, 0		/* *expected = current */
	li.w $a0, 0
	jr $ra

.section .note.GNU-stack,"",@progbits
