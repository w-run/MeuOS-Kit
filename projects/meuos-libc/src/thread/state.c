/* thread/state.c -- meuos_thread + tss + thread_controls globals.
 *
 * Defines the storage for the per-thread control block, the TSS key/
 * value tables, and the lock helpers used by every other thread/*.c
 * file. Kept separate so adding new TSS bookkeeping does not require
 * touching c11_threads.c. */

#include <stdatomic.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>
#include "internal.h"

struct tss_key_entry tss_keys[TSS_KEYS];
struct tss_value_entry tss_values[TSS_VALUES];
_Atomic int tss_guard;
struct meuos_thread *thread_controls[THREAD_CONTROLS];
_Atomic int thread_controls_guard;

void
__meuos_lock_tss(void)
{
	while (atomic_exchange(&tss_guard, 1))
		thrd_yield();
}

void
__meuos_unlock_tss(void)
{
	atomic_store(&tss_guard, 0);
}

static void
clear_tss_value(struct tss_value_entry *entry)
{
	memset(entry, 0, sizeof(*entry));
}

static void
lock_controls(void)
{
	while (atomic_exchange(&thread_controls_guard, 1))
		thrd_yield();
}

static void
unlock_controls(void)
{
	atomic_store(&thread_controls_guard, 0);
}

int
__meuos_control_add(struct meuos_thread *control)
{
	int index;

	lock_controls();
	for (index = 0; index < THREAD_CONTROLS; ++index) {
		if (!thread_controls[index]) {
			thread_controls[index] = control;
			unlock_controls();
			return 1;
		}
	}
	unlock_controls();
	return 0;
}

void
__meuos_control_remove(struct meuos_thread *control)
{
	int index;

	lock_controls();
	for (index = 0; index < THREAD_CONTROLS; ++index)
		if (thread_controls[index] == control)
			memset(&thread_controls[index], 0, sizeof(thread_controls[index]));
	unlock_controls();
}

struct meuos_thread *
__meuos_control_current(pid_t tid)
{
	struct meuos_thread *control = 0;
	int index;

	lock_controls();
	for (index = 0; index < THREAD_CONTROLS; ++index)
		if (thread_controls[index] && thread_controls[index]->tid == tid) {
			control = thread_controls[index];
			break;
		}
	unlock_controls();
	return control;
}

/* TSS destructor sweep, called from __meuos_thread_finish. */
void
__meuos_tss_cleanup(pid_t tid)
{
	int iteration;

	for (iteration = 0; iteration < TSS_DTOR_ITERATIONS; ++iteration) {
		int called = 0;
		int index;
		for (index = 0; index < TSS_VALUES; ++index) {
			tss_dtor_t destructor = 0;
			void *value = 0;

			__meuos_lock_tss();
			if (tss_values[index].tid == tid && tss_values[index].value
			 && tss_values[index].key > 0
			 && tss_values[index].key <= TSS_KEYS
			 && tss_keys[tss_values[index].key - 1].active) {
				destructor = tss_keys[tss_values[index].key - 1].destructor;
				value = tss_values[index].value;
				clear_tss_value(&tss_values[index]);
			}
			__meuos_unlock_tss();
			if (destructor) {
				destructor(value);
				called = 1;
			}
		}
		if (!called)
			return;
	}
}
