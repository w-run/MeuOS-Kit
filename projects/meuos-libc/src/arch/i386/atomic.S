.text

.globl atomic_thread_fence
atomic_thread_fence: mfence; ret
.globl atomic_signal_fence
atomic_signal_fence: ret

.globl __atomic_load_1
__atomic_load_1: movl 4(%esp), %eax; movzbl (%eax), %eax; ret
.globl __atomic_store_1
__atomic_store_1: movl 4(%esp), %ecx; movl 8(%esp), %eax; xchgb %al, (%ecx); ret
.globl __atomic_exchange_1
__atomic_exchange_1: movl 4(%esp), %ecx; movl 8(%esp), %eax; xchgb %al, (%ecx); movzbl %al, %eax; ret
.globl __atomic_fetch_add_1
__atomic_fetch_add_1: movl 4(%esp), %ecx; movl 8(%esp), %eax; lock xaddb %al, (%ecx); movzbl %al, %eax; ret
.globl __atomic_fetch_sub_1
__atomic_fetch_sub_1: movl 4(%esp), %ecx; movl 8(%esp), %eax; negb %al; lock xaddb %al, (%ecx); movzbl %al, %eax; ret
.globl __atomic_compare_exchange_1
__atomic_compare_exchange_1:
	pushl %ebx; movl 8(%esp), %edx; movl 12(%esp), %ecx; movzbl (%ecx), %eax; movl 16(%esp), %ebx; lock cmpxchgb %bl, (%edx); jne 1f; movl $1, %eax; popl %ebx; ret
1: movb %al, (%ecx); xorl %eax, %eax; popl %ebx; ret

.globl __atomic_load_2
__atomic_load_2: movl 4(%esp), %eax; movzwl (%eax), %eax; ret
.globl __atomic_store_2
__atomic_store_2: movl 4(%esp), %ecx; movl 8(%esp), %eax; xchgw %ax, (%ecx); ret
.globl __atomic_exchange_2
__atomic_exchange_2: movl 4(%esp), %ecx; movl 8(%esp), %eax; xchgw %ax, (%ecx); movzwl %ax, %eax; ret
.globl __atomic_fetch_add_2
__atomic_fetch_add_2: movl 4(%esp), %ecx; movl 8(%esp), %eax; lock xaddw %ax, (%ecx); movzwl %ax, %eax; ret
.globl __atomic_fetch_sub_2
__atomic_fetch_sub_2: movl 4(%esp), %ecx; movl 8(%esp), %eax; negw %ax; lock xaddw %ax, (%ecx); movzwl %ax, %eax; ret
.globl __atomic_compare_exchange_2
__atomic_compare_exchange_2:
	pushl %ebx; movl 8(%esp), %edx; movl 12(%esp), %ecx; movzwl (%ecx), %eax; movl 16(%esp), %ebx; lock cmpxchgw %bx, (%edx); jne 1f; movl $1, %eax; popl %ebx; ret
1: movw %ax, (%ecx); xorl %eax, %eax; popl %ebx; ret

.globl __atomic_load_4
__atomic_load_4: movl 4(%esp), %eax; movl (%eax), %eax; ret
.globl __atomic_store_4
__atomic_store_4: movl 4(%esp), %ecx; movl 8(%esp), %eax; xchgl %eax, (%ecx); ret
.globl __atomic_exchange_4
__atomic_exchange_4: movl 4(%esp), %ecx; movl 8(%esp), %eax; xchgl %eax, (%ecx); ret
.globl __atomic_fetch_add_4
__atomic_fetch_add_4: movl 4(%esp), %ecx; movl 8(%esp), %eax; lock xaddl %eax, (%ecx); ret
.globl __atomic_fetch_sub_4
__atomic_fetch_sub_4: movl 4(%esp), %ecx; movl 8(%esp), %eax; negl %eax; lock xaddl %eax, (%ecx); ret

.globl __atomic_fetch_and_4
__atomic_fetch_and_4:
	pushl %esi; movl 8(%esp), %edx; movl 12(%esp), %ecx
1: movl (%edx), %eax; movl %eax, %esi; andl %ecx, %esi; lock cmpxchgl %esi, (%edx); jne 1b; popl %esi; ret
.globl __atomic_fetch_or_4
__atomic_fetch_or_4:
	pushl %esi; movl 8(%esp), %edx; movl 12(%esp), %ecx
1: movl (%edx), %eax; movl %eax, %esi; orl %ecx, %esi; lock cmpxchgl %esi, (%edx); jne 1b; popl %esi; ret
.globl __atomic_fetch_xor_4
__atomic_fetch_xor_4:
	pushl %esi; movl 8(%esp), %edx; movl 12(%esp), %ecx
1: movl (%edx), %eax; movl %eax, %esi; xorl %ecx, %esi; lock cmpxchgl %esi, (%edx); jne 1b; popl %esi; ret

.globl __atomic_compare_exchange_4
__atomic_compare_exchange_4:
	pushl %esi; movl 8(%esp), %edx; movl 12(%esp), %ecx; movl (%ecx), %eax
	movl 16(%esp), %esi; lock cmpxchgl %esi, (%edx); jne 1f
	movl $1, %eax; popl %esi; ret
1: movl %eax, (%ecx); xorl %eax, %eax; popl %esi; ret

.section .note.GNU-stack,"",@progbits

/* ---- 8-byte (Kl) atomics via lock cmpxchg8b ----
 * On i386 there is no direct 8-byte RMW; each helper loops
 *   old = *p            (edx:eax)
 *   new = old <op> val  (ecx:ebx)
 *   lock cmpxchg8b (p)  -> success when edx:eax still == *p
 * returning the old value in edx:eax. */

.globl __atomic_load_8
__atomic_load_8:
	movl 4(%esp), %ecx
	movl (%ecx), %eax
	movl 4(%ecx), %edx
	ret

.globl __atomic_store_8
__atomic_store_8:
	movl 4(%esp), %ecx
	movl 8(%esp), %eax
	movl 12(%esp), %edx
	movl %eax, (%ecx)
	movl %edx, 4(%ecx)
	ret

.globl __atomic_exchange_8
__atomic_exchange_8:
	pushl %ebx
	pushl %esi
	movl 12(%esp), %esi
	movl 16(%esp), %ebx
	movl 20(%esp), %ecx
1:	movl (%esi), %eax
	movl 4(%esi), %edx
	lock cmpxchg8b (%esi)
	jne 1b
	popl %esi
	popl %ebx
	ret

.globl __atomic_fetch_add_8
__atomic_fetch_add_8:
	pushl %ebx
	pushl %esi
	movl 12(%esp), %esi
	movl 16(%esp), %ebx
	movl 20(%esp), %ecx
1:	movl (%esi), %eax
	movl 4(%esi), %edx
	movl %eax, %ebx
	movl %edx, %ecx
	addl 16(%esp), %ebx
	adcl 20(%esp), %ecx
	lock cmpxchg8b (%esi)
	jne 1b
	popl %esi
	popl %ebx
	ret

.globl __atomic_fetch_sub_8
__atomic_fetch_sub_8:
	pushl %ebx
	pushl %esi
	movl 12(%esp), %esi
	movl 16(%esp), %ebx
	movl 20(%esp), %ecx
1:	movl (%esi), %eax
	movl 4(%esi), %edx
	movl %eax, %ebx
	movl %edx, %ecx
	subl 16(%esp), %ebx
	sbbl 20(%esp), %ecx
	lock cmpxchg8b (%esi)
	jne 1b
	popl %esi
	popl %ebx
	ret

.globl __atomic_compare_exchange_8
__atomic_compare_exchange_8:
	pushl %ebx
	pushl %esi
	movl 12(%esp), %esi      /* ptr */
	movl 16(%esp), %ecx      /* expected ptr */
	movl (%ecx), %eax        /* expected_lo */
	movl 4(%ecx), %edx        /* expected_hi */
	movl 20(%esp), %ebx       /* desired_lo */
	movl 24(%esp), %ecx       /* desired_hi */
	lock cmpxchg8b (%esi)
	jne 1f
	movl $1, %eax
	popl %esi
	popl %ebx
	ret
1:	movl 16(%esp), %ecx
	movl %eax, (%ecx)
	movl %edx, 4(%ecx)
	xorl %eax, %eax
	popl %esi
	popl %ebx
	ret
