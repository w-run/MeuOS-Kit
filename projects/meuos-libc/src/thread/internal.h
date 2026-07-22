#ifndef MEUOS_THREAD_INTERNAL_H
#define MEUOS_THREAD_INTERNAL_H

/* Internal thread/runtime state shared by the split thread/*.c files.
 * Owns:
 *   - struct meuos_thread (the per-thread control block)
 *   - the futex syscall number constant
 *   - tss_keys / tss_values / thread_controls global arrays
 *   - the control_* / lock_tss accessors (defined in state.c)
 *
 * Kept in a header so c11_threads.c, tss.c, mutex.c, condvar.c,
 * call_once.c can share state without duplicating the layout. */

#include <stdatomic.h>
#include <stddef.h>
#include <sys/types.h>
#include <threads.h>

#if defined(__i386__)
/* i386 futex is 240; 202 is getegid32 on i386. */
#define LINUX_SYS_FUTEX 240
#else
#define LINUX_SYS_FUTEX 202
#endif
#define FUTEX_WAIT      0
#define THREAD_STACK_SIZE (1024 * 1024)
#define TSS_KEYS     32
#define TSS_VALUES   256
#define THREAD_CONTROLS 64

struct meuos_thread {
	int tid;
	int result;
	void *stack;
	void *tls;
	size_t tls_size;
};

struct tss_key_entry {
	int active;
	tss_dtor_t destructor;
};

struct tss_value_entry {
	pid_t tid;
	tss_t key;
	void *value;
};

extern struct tss_key_entry tss_keys[TSS_KEYS];
extern struct tss_value_entry tss_values[TSS_VALUES];
extern _Atomic int tss_guard;
extern struct meuos_thread *thread_controls[THREAD_CONTROLS];
extern _Atomic int thread_controls_guard;

/* ASM helpers declared by arch/<arch>/thread_clone.S. */
long __meuos_thread_clone(thrd_start_t, void *, void *, struct meuos_thread *, void *);

/* TLS helpers declared by arch/<arch>/tls.c. */
void *__meuos_tls_alloc(void);
size_t __meuos_tls_size(void);
void __meuos_tls_free(void *thread_pointer);

/* TSS destructor sweep, called from __meuos_thread_finish. */
void __meuos_tss_cleanup(pid_t tid);

/* Internal accessors defined in state.c. */
int __meuos_control_add(struct meuos_thread *control);
void __meuos_control_remove(struct meuos_thread *control);
struct meuos_thread *__meuos_control_current(pid_t tid);

void __meuos_lock_tss(void);
void __meuos_unlock_tss(void);

#endif
