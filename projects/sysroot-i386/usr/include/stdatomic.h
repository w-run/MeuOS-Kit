#ifndef MEUOS_STDATOMIC_H
#define MEUOS_STDATOMIC_H

/* ISO C11 atomic interface for mcc.  The compiler recognizes the builtin
 * spellings below and lowers them to the MeuOS atomic runtime ABI. */

typedef enum memory_order {
	memory_order_relaxed,
	memory_order_consume,
	memory_order_acquire,
	memory_order_release,
	memory_order_acq_rel,
	memory_order_seq_cst,
} memory_order;

typedef _Atomic _Bool atomic_bool;
typedef _Atomic char atomic_char;
typedef _Atomic signed char atomic_schar;
typedef _Atomic unsigned char atomic_uchar;
typedef _Atomic short atomic_short;
typedef _Atomic unsigned short atomic_ushort;
typedef _Atomic int atomic_int;
typedef _Atomic unsigned int atomic_uint;
typedef _Atomic long atomic_long;
typedef _Atomic unsigned long atomic_ulong;
typedef _Atomic long long atomic_llong;
typedef _Atomic unsigned long long atomic_ullong;
typedef atomic_bool atomic_flag;

#define ATOMIC_BOOL_LOCK_FREE 2
#define ATOMIC_CHAR_LOCK_FREE 2
#define ATOMIC_SHORT_LOCK_FREE 2
#define ATOMIC_INT_LOCK_FREE 2
#define ATOMIC_LONG_LOCK_FREE 2
#define ATOMIC_LLONG_LOCK_FREE 2

#define ATOMIC_VAR_INIT(value) (value)
#define ATOMIC_FLAG_INIT 0

#define atomic_init(object, value) (*(object) = (value))
#define atomic_load(object) (*(object))
#define atomic_store(object, value) (*(object) = (value))

#if defined(__GNUC__)
/* GCC and clang expose the standard __atomic family.  mcc intentionally has
 * smaller, typed atomic builtins; keep the public C11 interface portable
 * across both bootstrap compilers. */
#define atomic_exchange_explicit(object, value, order) \
	__atomic_exchange_n((object), (value), (order))
#define atomic_compare_exchange_strong_explicit(object, expected, desired, success, failure) \
	__atomic_compare_exchange_n((object), (expected), (desired), 0, (success), (failure))
#define atomic_compare_exchange_weak_explicit(object, expected, desired, success, failure) \
	__atomic_compare_exchange_n((object), (expected), (desired), 1, (success), (failure))
#define atomic_fetch_add_explicit(object, operand, order) \
	__atomic_fetch_add((object), (operand), (order))
#define atomic_fetch_sub_explicit(object, operand, order) \
	__atomic_fetch_sub((object), (operand), (order))
#define atomic_fetch_and_explicit(object, operand, order) \
	__atomic_fetch_and((object), (operand), (order))
#define atomic_fetch_or_explicit(object, operand, order) \
	__atomic_fetch_or((object), (operand), (order))
#define atomic_fetch_xor_explicit(object, operand, order) \
	__atomic_fetch_xor((object), (operand), (order))
#else
#define atomic_exchange_explicit(object, value, order) \
	__builtin_atomic_exchange((object), (value), (order))
#define atomic_compare_exchange_strong_explicit(object, expected, desired, success, failure) \
	__builtin_atomic_compare_exchange((object), (expected), (desired), 0, (success), (failure))
#define atomic_compare_exchange_weak_explicit(object, expected, desired, success, failure) \
	__builtin_atomic_compare_exchange((object), (expected), (desired), 1, (success), (failure))
#define atomic_fetch_add_explicit(object, operand, order) \
	__builtin_atomic_fetch_add((object), (operand), (order))
#define atomic_fetch_sub_explicit(object, operand, order) \
	__builtin_atomic_fetch_sub((object), (operand), (order))
#define atomic_fetch_and_explicit(object, operand, order) \
	__builtin_atomic_fetch_and((object), (operand), (order))
#define atomic_fetch_or_explicit(object, operand, order) \
	__builtin_atomic_fetch_or((object), (operand), (order))
#define atomic_fetch_xor_explicit(object, operand, order) \
	__builtin_atomic_fetch_xor((object), (operand), (order))
#endif
#define atomic_exchange(object, value) \
	atomic_exchange_explicit((object), (value), memory_order_seq_cst)

#define atomic_compare_exchange_strong(object, expected, desired) \
	atomic_compare_exchange_strong_explicit((object), (expected), (desired), \
		memory_order_seq_cst, memory_order_seq_cst)
#define atomic_compare_exchange_weak(object, expected, desired) \
	atomic_compare_exchange_weak_explicit((object), (expected), (desired), \
		memory_order_seq_cst, memory_order_seq_cst)

#define atomic_fetch_add(object, operand) atomic_fetch_add_explicit((object), (operand), memory_order_seq_cst)
#define atomic_fetch_sub(object, operand) atomic_fetch_sub_explicit((object), (operand), memory_order_seq_cst)
#define atomic_fetch_and(object, operand) atomic_fetch_and_explicit((object), (operand), memory_order_seq_cst)
#define atomic_fetch_or(object, operand) atomic_fetch_or_explicit((object), (operand), memory_order_seq_cst)
#define atomic_fetch_xor(object, operand) atomic_fetch_xor_explicit((object), (operand), memory_order_seq_cst)

#define atomic_flag_test_and_set(object) atomic_exchange((object), 1)
#define atomic_flag_test_and_set_explicit(object, order) atomic_exchange_explicit((object), 1, (order))
#define atomic_flag_clear(object) atomic_store((object), 0)
#define atomic_flag_clear_explicit(object, order) atomic_store((object), 0)

void atomic_thread_fence(memory_order);
void atomic_signal_fence(memory_order);

/* All naturally aligned integer widths represented by this initial x86_64
 * runtime use lock-prefixed instructions or xchg, so they are lock-free. */
#define atomic_is_lock_free(object) (sizeof(*(object)) <= 8)

#endif
