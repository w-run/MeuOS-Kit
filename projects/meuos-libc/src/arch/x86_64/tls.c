#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include "../../internal/syscall.h"

/* x86_64 ELF TLS uses variant II: static TLS is immediately below %fs. */
#define AT_NULL 0
#define AT_PHDR 3
#define AT_PHENT 4
#define AT_PHNUM 5
#define PT_TLS 7
#define LINUX_SYS_MMAP 9
#define ARCH_SET_FS 0x1002

struct meuos_auxv { unsigned long type; unsigned long value; };
struct meuos_phdr {
	uint32_t type, flags;
	uint64_t offset, virtual_address, physical_address;
	uint64_t file_size, memory_size, alignment;
};

extern long __meuos_arch_prctl(long, unsigned long);

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
	char *memory, *thread_pointer;
	long result;
	size_t tls_end;

	if (!tls_memory_size)
		return 0;
	tls_end = round_up(tls_memory_size, tls_alignment);
	tls_allocation_size = tls_end + sizeof(void *);
	result = __syscall6(LINUX_SYS_MMAP, 0, tls_allocation_size,
		PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (__syscall_error(result))
		return 0;
	memory = (char *)result;
	thread_pointer = memory + tls_end;
	memset(memory, 0, tls_allocation_size);
	memcpy(thread_pointer - tls_memory_size, tls_image, tls_file_size);
	*(void **)thread_pointer = thread_pointer;
	return thread_pointer;
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
		if (!thread_pointer || __meuos_arch_prctl(ARCH_SET_FS, (unsigned long)thread_pointer) < 0)
			return;
		return;
	}
}

void *
__meuos_tls_alloc(void) { return allocate_tls(); }
size_t __meuos_tls_size(void) { return tls_allocation_size; }
