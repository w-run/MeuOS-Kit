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
__atomic_fetch_sub_1: movl 4(%esp), %ecx; movl 8(%esp), %eax; negb %al; lock xaddb %al, (%ecx); negb %al; movzbl %al, %eax; ret
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
__atomic_fetch_sub_2: movl 4(%esp), %ecx; movl 8(%esp), %eax; negw %ax; lock xaddw %ax, (%ecx); negw %ax; movzwl %ax, %eax; ret
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
__atomic_fetch_sub_4: movl 4(%esp), %ecx; movl 8(%esp), %eax; negl %eax; lock xaddl %eax, (%ecx); negl %eax; ret

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
