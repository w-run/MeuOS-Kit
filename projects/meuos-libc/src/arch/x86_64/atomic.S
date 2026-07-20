.text

/* MeuOS compiler atomic runtime, x86-64 System V ABI.
 * mcc currently requests these symbols for 32-bit atomic integers.  x86 XCHG
 * is implicitly locked; LOCK XADD/CMPXCHG provide the seq_cst RMW operations.
 */

.globl atomic_thread_fence
.type atomic_thread_fence,@function
atomic_thread_fence:
	mfence
	ret

.globl atomic_signal_fence
.type atomic_signal_fence,@function
atomic_signal_fence:
	ret

.globl __atomic_load_1
.type __atomic_load_1,@function
__atomic_load_1:
	movzbl (%rdi), %eax
	ret

.globl __atomic_store_1
.type __atomic_store_1,@function
__atomic_store_1:
	xchgb %sil, (%rdi)
	ret

.globl __atomic_exchange_1
.type __atomic_exchange_1,@function
__atomic_exchange_1:
	xchgb %sil, (%rdi)
	movzbl %sil, %eax
	ret

.globl __atomic_fetch_add_1
.type __atomic_fetch_add_1,@function
__atomic_fetch_add_1:
	movb %sil, %al
	lock xaddb %al, (%rdi)
	movzbl %al, %eax
	ret

.globl __atomic_fetch_sub_1
.type __atomic_fetch_sub_1,@function
__atomic_fetch_sub_1:
	negb %sil
	movb %sil, %al
	lock xaddb %al, (%rdi)
	movzbl %al, %eax
	ret

.globl __atomic_fetch_and_1
.type __atomic_fetch_and_1,@function
__atomic_fetch_and_1:
.Land1:
	movzbl (%rdi), %eax
	movl %eax, %ecx
	andl %esi, %ecx
	lock cmpxchgb %cl, (%rdi)
	jne .Land1
	movzbl %al, %eax
	ret

.globl __atomic_fetch_or_1
.type __atomic_fetch_or_1,@function
__atomic_fetch_or_1:
.Lor1:
	movzbl (%rdi), %eax
	movl %eax, %ecx
	orl %esi, %ecx
	lock cmpxchgb %cl, (%rdi)
	jne .Lor1
	movzbl %al, %eax
	ret

.globl __atomic_fetch_xor_1
.type __atomic_fetch_xor_1,@function
__atomic_fetch_xor_1:
.Lxor1:
	movzbl (%rdi), %eax
	movl %eax, %ecx
	xorl %esi, %ecx
	lock cmpxchgb %cl, (%rdi)
	jne .Lxor1
	movzbl %al, %eax
	ret

.globl __atomic_compare_exchange_1
.type __atomic_compare_exchange_1,@function
__atomic_compare_exchange_1:
	movzbl (%rsi), %eax
	lock cmpxchgb %dl, (%rdi)
	jne .Lcmpxchg1_fail
	movl $1, %eax
	ret
.Lcmpxchg1_fail:
	movb %al, (%rsi)
	xorl %eax, %eax
	ret

.globl __atomic_load_2
.type __atomic_load_2,@function
__atomic_load_2:
	movzwl (%rdi), %eax
	ret

.globl __atomic_store_2
.type __atomic_store_2,@function
__atomic_store_2:
	xchgw %si, (%rdi)
	ret

.globl __atomic_exchange_2
.type __atomic_exchange_2,@function
__atomic_exchange_2:
	xchgw %si, (%rdi)
	movzwl %si, %eax
	ret

.globl __atomic_fetch_add_2
.type __atomic_fetch_add_2,@function
__atomic_fetch_add_2:
	movw %si, %ax
	lock xaddw %ax, (%rdi)
	movzwl %ax, %eax
	ret

.globl __atomic_fetch_sub_2
.type __atomic_fetch_sub_2,@function
__atomic_fetch_sub_2:
	negw %si
	movw %si, %ax
	lock xaddw %ax, (%rdi)
	movzwl %ax, %eax
	ret

.globl __atomic_fetch_and_2
.type __atomic_fetch_and_2,@function
__atomic_fetch_and_2:
.Land2:
	movzwl (%rdi), %eax
	movl %eax, %ecx
	andl %esi, %ecx
	lock cmpxchgw %cx, (%rdi)
	jne .Land2
	movzwl %ax, %eax
	ret

.globl __atomic_fetch_or_2
.type __atomic_fetch_or_2,@function
__atomic_fetch_or_2:
.Lor2:
	movzwl (%rdi), %eax
	movl %eax, %ecx
	orl %esi, %ecx
	lock cmpxchgw %cx, (%rdi)
	jne .Lor2
	movzwl %ax, %eax
	ret

.globl __atomic_fetch_xor_2
.type __atomic_fetch_xor_2,@function
__atomic_fetch_xor_2:
.Lxor2:
	movzwl (%rdi), %eax
	movl %eax, %ecx
	xorl %esi, %ecx
	lock cmpxchgw %cx, (%rdi)
	jne .Lxor2
	movzwl %ax, %eax
	ret

.globl __atomic_compare_exchange_2
.type __atomic_compare_exchange_2,@function
__atomic_compare_exchange_2:
	movzwl (%rsi), %eax
	lock cmpxchgw %dx, (%rdi)
	jne .Lcmpxchg2_fail
	movl $1, %eax
	ret
.Lcmpxchg2_fail:
	movw %ax, (%rsi)
	xorl %eax, %eax
	ret

.globl __atomic_load_4
.type __atomic_load_4,@function
__atomic_load_4:
	movl (%rdi), %eax
	ret

.globl __atomic_store_4
.type __atomic_store_4,@function
__atomic_store_4:
	xchgl %esi, (%rdi)
	ret

.globl __atomic_exchange_4
.type __atomic_exchange_4,@function
__atomic_exchange_4:
	xchgl %esi, (%rdi)
	movl %esi, %eax
	ret

.globl __atomic_fetch_add_4
.type __atomic_fetch_add_4,@function
__atomic_fetch_add_4:
	movl %esi, %eax
	lock xaddl %eax, (%rdi)
	ret

.globl __atomic_fetch_sub_4
.type __atomic_fetch_sub_4,@function
__atomic_fetch_sub_4:
	negl %esi
	movl %esi, %eax
	lock xaddl %eax, (%rdi)
	ret

.globl __atomic_fetch_and_4
.type __atomic_fetch_and_4,@function
__atomic_fetch_and_4:
.Land4:
	movl (%rdi), %eax
	movl %eax, %ecx
	andl %esi, %ecx
	lock cmpxchgl %ecx, (%rdi)
	jne .Land4
	ret

.globl __atomic_fetch_or_4
.type __atomic_fetch_or_4,@function
__atomic_fetch_or_4:
.Lor4:
	movl (%rdi), %eax
	movl %eax, %ecx
	orl %esi, %ecx
	lock cmpxchgl %ecx, (%rdi)
	jne .Lor4
	ret

.globl __atomic_fetch_xor_4
.type __atomic_fetch_xor_4,@function
__atomic_fetch_xor_4:
.Lxor4:
	movl (%rdi), %eax
	movl %eax, %ecx
	xorl %esi, %ecx
	lock cmpxchgl %ecx, (%rdi)
	jne .Lxor4
	ret

.globl __atomic_compare_exchange_4
.type __atomic_compare_exchange_4,@function
__atomic_compare_exchange_4:
	movl (%rsi), %eax
	lock cmpxchgl %edx, (%rdi)
	jne .Lcmpxchg4_fail
	movl $1, %eax
	ret
.Lcmpxchg4_fail:
	movl %eax, (%rsi)
	xorl %eax, %eax
	ret

.globl __atomic_load_8
.type __atomic_load_8,@function
__atomic_load_8:
	movq (%rdi), %rax
	ret

.globl __atomic_store_8
.type __atomic_store_8,@function
__atomic_store_8:
	xchgq %rsi, (%rdi)
	ret

.globl __atomic_exchange_8
.type __atomic_exchange_8,@function
__atomic_exchange_8:
	xchgq %rsi, (%rdi)
	movq %rsi, %rax
	ret

.globl __atomic_fetch_add_8
.type __atomic_fetch_add_8,@function
__atomic_fetch_add_8:
	movq %rsi, %rax
	lock xaddq %rax, (%rdi)
	ret

.globl __atomic_fetch_sub_8
.type __atomic_fetch_sub_8,@function
__atomic_fetch_sub_8:
	negq %rsi
	movq %rsi, %rax
	lock xaddq %rax, (%rdi)
	ret

.globl __atomic_fetch_and_8
.type __atomic_fetch_and_8,@function
__atomic_fetch_and_8:
.Land8:
	movq (%rdi), %rax
	movq %rax, %rcx
	andq %rsi, %rcx
	lock cmpxchgq %rcx, (%rdi)
	jne .Land8
	ret

.globl __atomic_fetch_or_8
.type __atomic_fetch_or_8,@function
__atomic_fetch_or_8:
.Lor8:
	movq (%rdi), %rax
	movq %rax, %rcx
	orq %rsi, %rcx
	lock cmpxchgq %rcx, (%rdi)
	jne .Lor8
	ret

.globl __atomic_fetch_xor_8
.type __atomic_fetch_xor_8,@function
__atomic_fetch_xor_8:
.Lxor8:
	movq (%rdi), %rax
	movq %rax, %rcx
	xorq %rsi, %rcx
	lock cmpxchgq %rcx, (%rdi)
	jne .Lxor8
	ret

.globl __atomic_compare_exchange_8
.type __atomic_compare_exchange_8,@function
__atomic_compare_exchange_8:
	movq (%rsi), %rax
	lock cmpxchgq %rdx, (%rdi)
	jne .Lcmpxchg8_fail
	movl $1, %eax
	ret
.Lcmpxchg8_fail:
	movq %rax, (%rsi)
	xorl %eax, %eax
	ret

.section .note.GNU-stack,"",@progbits
