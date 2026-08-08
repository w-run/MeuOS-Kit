#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include "../../internal/syscall.h"

/* armv7 ELF TLS uses variant I: static TLS lives at non-negative
 * offsets from the thread pointer (cp15 c13).  mcc emits local-exec
 * TLS access as:
 *
 *     mrc   R, p15, 0, c13, c0, 3   ; read TPIDRURW/c13
 *     add   R, R, #:tprel_hi12:S, lsl #12
 *     add   R, R, #:tprel_lo12_nc:S
 *
 * For static binaries the linker bakes the symbol's offset within
 * the PT_TLS image into the addend, plus a MEUOS_TLS_GAP reservation
 * that matches the musl armv7 ABI (TCB header lives just below .tdata).
 * Concretely TP must address the start of the mmap allocation; .tdata
 * starts at TP+GAP; user-visible TLS reads at TP+0x10 land on .tdata[0].
 *
 * The kernel programs the TP via CLONE_SETTLS when supplied to clone(2);
 * the main thread installs it via `mcr p15,0,r0,c13,c0,3` in set_tls.S.
 * Threads release the TLS block with __meuos_tls_free(tp) (passing the
 * user-visible TP, i.e. the mmap base). */

#define AT_NULL 0
#define AT_PHDR 3
#define AT_PHENT 4
#define AT_PHNUM 5
#define PT_PHDR 6
#define PT_TLS 7
/* ARM EABI: mmap2(192) and munmap(91). */
#define ARM_SYS_MMAP   192
#define ARM_SYS_MUNMAP 91

struct meuos_auxv { unsigned long type; unsigned long value; };
struct meuos_phdr {
	uint32_t type, flags;
	uint32_t offset, virtual_address;
	uint32_t file_size, memory_size;
	uint32_t alignment;
};

extern void __meuos_set_tls(void *tp);

#define MEUOS_TLS_GAP 16

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
	tls_allocation_size = round_up(tls_memory_size + MEUOS_TLS_GAP, tls_alignment);
	result = __syscall6(ARM_SYS_MMAP, 0, tls_allocation_size,
		PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (__syscall_error(result))
		return 0;
	memory = (char *)result;
	memcpy(memory + MEUOS_TLS_GAP, tls_image, tls_file_size);
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
	unsigned long load_base = 0;
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

	/* First pass: compute load_base from PT_PHDR so we can fix up
	 * PT_TLS virtual_address for PIE executables.
	 *   load_base = AT_PHDR - PT_PHDR.p_vaddr
	 * For non-PIE (position-dependent) this yields zero; for PIE it
	 * gives the actual load offset. */
	for (index = 0; index < count; ++index) {
		struct meuos_phdr *header = (struct meuos_phdr *)((char *)headers + index * entry_size);
		if (header->type == PT_PHDR) {
			load_base = (unsigned long)headers - header->virtual_address;
			break;
		}
	}

	for (index = 0; index < count; ++index) {
		struct meuos_phdr *header = (struct meuos_phdr *)((char *)headers + index * entry_size);
		if (header->type != PT_TLS)
			continue;
		if (!header->memory_size || !header->alignment || (header->alignment & (header->alignment - 1)))
			return;
		/* PT_TLS p_vaddr is file-relative; add load_base for PIE. */
		tls_image = (const void *)(load_base + (uintptr_t)header->virtual_address);
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

void
__meuos_tls_free(void *thread_pointer)
{
	if (thread_pointer && tls_allocation_size)
		__syscall6(ARM_SYS_MUNMAP, (long)thread_pointer,
			tls_allocation_size, 0, 0, 0, 0);
}
