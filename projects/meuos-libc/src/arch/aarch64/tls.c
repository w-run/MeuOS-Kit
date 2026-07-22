#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include "../../internal/syscall.h"

/* aarch64 ELF TLS uses variant I: static TLS lives at non-negative
 * offsets from the thread pointer (TPIDR_EL0).  mcc emits local-exec
 * TLS access as:
 *
 *     mrs   R, tpidr_el0
 *     add   R, R, #:tprel_hi12:S, lsl #12
 *     add   R, R, #:tprel_lo12_nc:S
 *
 * For static binaries the linker bakes the symbol's offset within
 * the PT_TLS image into the addend, plus a 16-byte GAP_ABOVE_TP
 * reservation that matches the musl aarch64 ABI (the TCB header lives
 * just below .tdata).  Concretely TPIDR_EL0 must address the start of
 * the mmap allocation; .tdata starts at TP+16; user-visible TLS
 * reads at TP+0x10 land on .tdata[0].  Without the gap the access
 * reads garbage from neighbouring pages.
 *
 * The kernel programs TPIDR_EL0 directly when CLONE_SETTLS is
 * supplied to clone(2); the main thread installs it via `msr tpidr_el0`
 * in set_tls.S.  Threads release the TLS block with __meuos_tls_free(tp)
 * (passing the user-visible TP, i.e. the mmap base). */

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

/* musl uses GAP_ABOVE_TP = 16 on aarch64 to leave room for the TCB
 * header that lives just below the TLS data.  The compiler emits
 * local-exec TLS accesses as `mrs R, tpidr_el0; add R, R, #tprel`
 * where tprel is the symbol's offset within the TLS segment; the
 * static linker bakes the GAP into the addend so an access like
 * `*((int*)TP+0x10)` actually reads .tdata[0].  We follow the same
 * layout: TPIDR_EL0 points at the TCB header (mmap base), .tdata is
 * copied to mmap_base + GAP, and __meuos_tls_free must release the
 * whole mmap region (not TP+something) since the user address is the
 * TCB pointer itself. */
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
	/* Reserve MEUOS_TLS_GAP bytes at the low end of the allocation so
	 * the thread pointer (the start of the region) addresses a small
	 * TCB header instead of .tdata itself.  The linker-baked tprel
	 * already accounts for this gap (see MEUOS_TLS_GAP comment). */
	tls_allocation_size = round_up(tls_memory_size + MEUOS_TLS_GAP, tls_alignment);
	result = __syscall6(AARCH64_SYS_MMAP, 0, tls_allocation_size,
		PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (__syscall_error(result))
		return 0;
	memory = (char *)result;
	/* mmap already zeroes the whole region (including the TCB gap);
	 * copy .tdata into place at memory + GAP. */
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

/* thread_pointer is the user-visible TP (mmap base, TPIDR_EL0 value);
 * munmap releases the entire allocation that backs it. */
void
__meuos_tls_free(void *thread_pointer)
{
	if (thread_pointer && tls_allocation_size)
		__syscall6(AARCH64_SYS_MUNMAP, (long)thread_pointer,
			tls_allocation_size, 0, 0, 0, 0);
}
