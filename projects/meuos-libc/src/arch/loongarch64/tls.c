#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include "../../internal/syscall.h"

/* loongarch64 ELF TLS uses variant I with GAP_ABOVE_TP = 0 (musl loongarch64
 * ABI, confirmed by pthread_arch.h: TLS_ABOVE_TP / GAP_ABOVE_TP 0).  The
 * thread pointer ($tp, r21) addresses the start of the TLS image directly,
 * so .tdata is copied to mmap_base + 0.  The kernel programs $tp via
 * CLONE_SETTLS for new threads; the main thread installs it in crt1.S via
 * `move $tp, $a0` after this returns the tp base pointer (loongarch64 has
 * no arch_prctl-like syscall). */

#define AT_NULL 0
#define AT_PHDR 3
#define AT_PHENT 4
#define AT_PHNUM 5
#define PT_PHDR 6
#define PT_TLS 7
/* loongarch64 uses the asm-generic syscall numbers (identical to aarch64/
 * riscv64); the internal __syscall_number() table in internal/syscall.h
 * translates our x86_64-style internal IDs to them. */
#define LA64_SYS_MMAP   222
#define LA64_SYS_MUNMAP 215

struct meuos_auxv { unsigned long type; unsigned long value; };
struct meuos_phdr {
	uint32_t type, flags;
	uint64_t offset, virtual_address, physical_address;
	uint64_t file_size, memory_size, alignment;
};

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
	/* GAP_ABOVE_TP = 0: tp points at the TLS image start directly. */
	tls_allocation_size = round_up(tls_memory_size, tls_alignment);
	result = __syscall6(LA64_SYS_MMAP, 0, tls_allocation_size,
		PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (__syscall_error(result))
		return 0;
	memory = (char *)result;
	/* mmap already zeroes the whole region; copy .tdata to base+0. */
	memcpy(memory, tls_image, tls_file_size);
	return memory;
}

/* Returns the TLS base pointer.  The caller (crt1.S) installs it into $tp
 * with `move $tp, $a0`; new threads get it via CLONE_SETTLS in
 * thread_clone.S. */
void *
__meuos_tls_init(char **environment)
{
	struct meuos_auxv *auxv;
	struct meuos_phdr *headers = 0;
	/* NOTE: entry_size and count are accessed via pointers to force the
	 * compiler to allocate them on the stack.  mcc's register allocator
	 * (rega/spill) does not reliably spill temporary registers back to
	 * the stack slot, causing the update to be lost when the register
	 * is reused later in the loop.  The pointer indirection forces the
	 * compiler to keep the stack slot up to date. */
	unsigned long entry_value = 0, count_value = 0;
	unsigned long *entry_size = &entry_value, *count = &count_value;
	unsigned long load_base = 0, index;
	void *thread_pointer;

	while (*environment)
		++environment;
	auxv = (struct meuos_auxv *)(environment + 1);
	for (; auxv->type != AT_NULL; ++auxv) {
		if (auxv->type == AT_PHDR) headers = (struct meuos_phdr *)auxv->value;
		else if (auxv->type == AT_PHENT) *entry_size = auxv->value;
		else if (auxv->type == AT_PHNUM) *count = auxv->value;
	}
	if (!headers || *entry_size != sizeof(*headers))
		return 0;

	/* First pass: compute load_base from PT_PHDR so we can fix up
	 * PT_TLS virtual_address for PIE executables.
	 *   load_base = AT_PHDR - PT_PHDR.p_vaddr
	 * For non-PIE (position-dependent) this yields zero; for PIE it
	 * gives the actual load offset. */
	for (index = 0; index < *count; ++index) {
		struct meuos_phdr *header = (struct meuos_phdr *)((char *)headers + index * *entry_size);
		if (header->type == PT_PHDR) {
			load_base = (unsigned long)headers - (unsigned long)header->virtual_address;
			break;
		}
	}

	for (index = 0; index < *count; ++index) {
		struct meuos_phdr *header = (struct meuos_phdr *)((char *)headers + index * *entry_size);
		if (header->type != PT_TLS)
			continue;
		if (!header->memory_size || !header->alignment || (header->alignment & (header->alignment - 1)))
			return 0;
		/* PT_TLS p_vaddr is file-relative; add load_base for PIE. */
		tls_image = (const void *)(load_base + (uintptr_t)header->virtual_address);
		tls_file_size = (size_t)header->file_size;
		tls_memory_size = (size_t)header->memory_size;
		tls_alignment = (size_t)header->alignment;
		thread_pointer = allocate_tls();
		if (!thread_pointer)
			return 0;
		return thread_pointer;
	}
	return 0;
}

void *
__meuos_tls_alloc(void) { return allocate_tls(); }
size_t __meuos_tls_size(void) { return tls_allocation_size; }

/* thread_pointer is the user-visible TP (mmap base); munmap releases the
 * entire allocation that backs it. */
void
__meuos_tls_free(void *thread_pointer)
{
	if (thread_pointer && tls_allocation_size)
		__syscall6(LA64_SYS_MUNMAP, (long)thread_pointer,
			tls_allocation_size, 0, 0, 0, 0);
}
