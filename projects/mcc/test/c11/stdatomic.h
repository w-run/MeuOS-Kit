#ifndef MCC_TEST_STDATOMIC_H
#define MCC_TEST_STDATOMIC_H

/* Minimal C11-facing shim used by the compiler regression tests.  MeuOS
 * libc will install the public version; these macros deliberately exercise
 * mcc's target-neutral builtin path rather than a host compiler builtin. */
typedef _Atomic int atomic_int;
typedef _Atomic int atomic_flag;

#define ATOMIC_FLAG_INIT 0

enum memory_order {
	memory_order_relaxed,
	memory_order_consume,
	memory_order_acquire,
	memory_order_release,
	memory_order_acq_rel,
	memory_order_seq_cst,
};

#define atomic_fetch_add_explicit(object, operand, order) \
	__builtin_atomic_fetch_add((object), (operand), (order))
#define atomic_fetch_sub_explicit(object, operand, order) \
	__builtin_atomic_fetch_sub((object), (operand), (order))
#define atomic_fetch_add(object, operand) \
	atomic_fetch_add_explicit((object), (operand), memory_order_seq_cst)
#define atomic_fetch_sub(object, operand) \
	atomic_fetch_sub_explicit((object), (operand), memory_order_seq_cst)
#define atomic_fetch_and(object, operand) \
	__builtin_atomic_fetch_and((object), (operand), memory_order_seq_cst)
#define atomic_fetch_or(object, operand) \
	__builtin_atomic_fetch_or((object), (operand), memory_order_seq_cst)
#define atomic_fetch_xor(object, operand) \
	__builtin_atomic_fetch_xor((object), (operand), memory_order_seq_cst)
#define atomic_exchange(object, desired) \
	__builtin_atomic_exchange((object), (desired), memory_order_seq_cst)
#define atomic_compare_exchange_strong(object, expected, desired) \
	__builtin_atomic_compare_exchange((object), (expected), (desired), 0, \
		memory_order_seq_cst, memory_order_seq_cst)
#define atomic_load(object) (*(object))
#define atomic_store(object, desired) (*(object) = (desired))
#define atomic_init(object, value) (*(object) = (value))
#define atomic_flag_test_and_set(object) atomic_exchange((object), 1)
#define atomic_flag_clear(object) atomic_store((object), 0)

#endif
