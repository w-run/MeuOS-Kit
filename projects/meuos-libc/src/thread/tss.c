/* thread/tss.c -- thread-specific storage (tss_create/get/set/delete).
 *
 * Implements the C11 <threads.h> TSS API over a fixed-size global
 * key/value table guarded by tss_guard (see state.c). Destructor
 * invocation happens from __meuos_tss_cleanup when a thread exits. */

#include <stddef.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>
#include "internal.h"

int
tss_create(tss_t *key, tss_dtor_t destructor)
{
	int index;

	if (!key)
		return thrd_error;
	__meuos_lock_tss();
	for (index = 0; index < TSS_KEYS; ++index)
		if (!tss_keys[index].active) {
			tss_keys[index].active = 1;
			tss_keys[index].destructor = destructor;
			*key = (tss_t)index + 1;
			__meuos_unlock_tss();
			return thrd_success;
		}
	__meuos_unlock_tss();
	return thrd_nomem;
}

void
tss_delete(tss_t key)
{
	int index;

	if (!key || key > TSS_KEYS)
		return;
	__meuos_lock_tss();
	memset(&tss_keys[key - 1], 0, sizeof(tss_keys[key - 1]));
	for (index = 0; index < TSS_VALUES; ++index)
		if (tss_values[index].key == key) {
			memset(&tss_values[index], 0, sizeof(tss_values[index]));
		}
	__meuos_unlock_tss();
}

void *
tss_get(tss_t key)
{
	pid_t tid = gettid();
	int index;
	void *value = 0;

	if (!key || key > TSS_KEYS)
		return 0;
	__meuos_lock_tss();
	if (tss_keys[key - 1].active)
		for (index = 0; index < TSS_VALUES; ++index)
			if (tss_values[index].tid == tid && tss_values[index].key == key) {
				value = tss_values[index].value;
				break;
			}
	__meuos_unlock_tss();
	return value;
}

int
tss_set(tss_t key, void *value)
{
	pid_t tid = gettid();
	int empty = -1;
	int index;

	if (!key || key > TSS_KEYS)
		return thrd_error;
	__meuos_lock_tss();
	if (!tss_keys[key - 1].active) {
		__meuos_unlock_tss();
		return thrd_error;
	}
	for (index = 0; index < TSS_VALUES; ++index) {
		if (tss_values[index].tid == tid && tss_values[index].key == key) {
			if (value)
				tss_values[index].value = value;
			else {
				memset(&tss_values[index], 0, sizeof(tss_values[index]));
			}
			__meuos_unlock_tss();
			return thrd_success;
		}
		if (empty < 0 && !tss_values[index].tid)
			empty = index;
	}
	if (!value) {
		__meuos_unlock_tss();
		return thrd_success;
	}
	if (empty < 0) {
		__meuos_unlock_tss();
		return thrd_nomem;
	}
	tss_values[empty].tid = tid;
	tss_values[empty].key = key;
	tss_values[empty].value = value;
	__meuos_unlock_tss();
	return thrd_success;
}
