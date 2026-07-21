#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include "../../internal/syscall.h"

/* aarch64 ELF TLS uses variant I: static TLS lives at or above the
 * thread pointer (TPIDR_EL0).  The thread pointer addresses the start
 * of the TLS block, and TLS variables are reached at non-negative
 * offsets from it.  mcc emits local-exec TLS access as:
 *
 *     mrs   R, tpidr_el0
 *     add   R, R, #:tprel_hi12:S, lsl #12
 *     add   R, R, #:tprel_lo12_nc:S
 *
 * The kernel programs TPIDR_EL0 directly when CLONE_SETTLS is supplied
 * to clone(2); the main thread installs it here via `msr tpidr_el0`.
 * Unlike variant II (x86_64/i386) there is no self-referential slot at
 * *(void **)TP, so threads release their TLS block with
 * __meuos_tls_free(tp) instead of recomputing the mmap base from TP. */

#define AT_NULL 0
#define AT_PHDR 3
#define AT_PHENT 4
#define AT_PHNUM 5
#define PT_TLS 7
/* aarch64 asm-generic syscall numbers.  Bypass the x86_64->aarch64
 * translation table by issuing the aarch64 numbers directly, matching
 * how i386 tls.c uses mmap2 (192) directly. */
#define AARCH64_SYS_MMAP   222
#define AARCH64_SYS_MUNMAP 215

struct meuos_auxv { unsigned long type; unsigned long value; };
struct meuos_phdr {
	uint32_t type, flags;
	uint64_t offset, virtual_address, physical_address;
	uint64_t file_size, memory_size, alignment;
};

/* aarch64 has no syscall to set TPIDR_EL0 (unlike x86_64 arch_prctl);
 * the register is programmed directly via `msr tpidr_el0, x0` in the
 * companion set_tls.S helper.  mcc does not support inline asm, so the
 * MSR lives in its own tiny assembly stub. */
extern void __meuos_set_tls(void *tp);

static const void *tls_image;
static size_t tls_file_size, tls_memory_size, tls_alignment, tls_allocation_size;

static size_t
round_up(size_t value, size_t alignment)
{
	return (value + alignment - 1) & ~(alignment - 1);
}

static void *
allocate_tls(void)
{
	char *memory;
	long result;

	if (!tls_memory_size)
		return 0;
	tls_allocation_size = round_up(tls_memory_size, tls_alignment);
	result = __syscall6(AARCH64_SYS_MMAP, 0, tls_allocation_size,
		PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (__syscall_error(result))
		return 0;
	memory = (char *)result;
	/* mmap already zeroes the whole region; copy .tdata into place.
	 * Variant I: TLS data begins at the thread pointer (memory + 0). */
	memcpy(memory, tls_image, tls_file_size);
	return memory;
}

static inline void
set_thread_pointer(void *tp)
{
	__meuos_set_tls(tp);
}

void
__meuos_tls_init(char **environment)
{
	struct meuos_auxv *auxv;
	struct meuos_phdr *headers = 0;
	unsigned long entry_size = 0, count = 0, index;
	void *thread_pointer;

	while (*environment)
		++environment;
	auxv = (struct meuos_auxv *)(environment + 1);
	for (; auxv->type != AT_NULL; ++auxv) {
		if (auxv->type == AT_PHDR) headers = (struct meuos_phdr *)auxv->value;
		else if (auxv->type == AT_PHENT) entry_size = auxv->value;
		else if (auxv->type == AT_PHNUM) count = auxv->value;
	}
	if (!headers || entry_size != sizeof(*headers))
		return;
	for (index = 0; index < count; ++index) {
		struct meuos_phdr *header = (struct meuos_phdr *)((char *)headers + index * entry_size);
		if (header->type != PT_TLS)
			continue;
		if (!header->memory_size || !header->alignment || (header->alignment & (header->alignment - 1)))
			return;
		tls_image = (const void *)(uintptr_t)header->virtual_address;
		tls_file_size = (size_t)header->file_size;
		tls_memory_size = (size_t)header->memory_size;
		tls_alignment = (size_t)header->alignment;
		thread_pointer = allocate_tls();
		if (!thread_pointer)
			return;
		set_thread_pointer(thread_pointer);
		return;
	}
}

void *
__meuos_tls_alloc(void) { return allocate_tls(); }
size_t __meuos_tls_size(void) { return tls_allocation_size; }

/* Variant I has no self-referential slot above TP, so the mmap base
 * equals the thread pointer itself. */
void
__meuos_tls_free(void *thread_pointer)
{
	if (thread_pointer && tls_allocation_size)
		__syscall6(AARCH64_SYS_MUNMAP, (long)thread_pointer,
			tls_allocation_size, 0, 0, 0, 0);
}
