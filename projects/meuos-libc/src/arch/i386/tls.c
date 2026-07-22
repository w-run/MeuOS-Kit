#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include "../../internal/syscall.h"

/* i386 ELF TLS uses variant II: static TLS is immediately below the thread
 * pointer, which is stored as the %gs segment base.  The thread pointer
 * (TP) is self-referential: *(void **)TP == TP.  mcc emits local-exec TLS
 * access as:
 *     movl %gs:0, %reg        ; load TP
 *     leal  symbol(%reg), %reg ; TP + negative TLS offset
 *
 * The %gs base is configured per-thread via the set_thread_area(2) system
 * call, which installs a GDT descriptor.  Each thread reuses the same GDT
 * entry number (assigned once for the main thread, then replicated in
 * clone children). */

#define AT_NULL 0
#define AT_PHDR 3
#define AT_PHENT 4
#define AT_PHNUM 5
#define PT_TLS 7
/* i386 set_thread_area(2) descriptor.  The kernel writes entry_number
 * back when it allocates a GDT slot. */
struct meuos_user_desc {
	unsigned int entry_number;
	unsigned int base_addr;
	unsigned int limit;
	unsigned int flags;
};

/* ELF32 program header (32 bytes). */
struct meuos_phdr32 {
	uint32_t p_type, p_offset, p_vaddr, p_paddr;
	uint32_t p_filesz, p_memsz, p_flags, p_align;
};

struct meuos_auxv32 { unsigned int type; unsigned int value; };

extern void __meuos_load_gs(unsigned short selector);

static const void *tls_image;
static size_t tls_file_size, tls_memory_size, tls_alignment, tls_allocation_size;
static unsigned int tls_entry_number = 0xFFFFFFFF;

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
	/* i386 has no mmap (9 is link on i386); use mmap2 (192) with the
	 * offset given in 4096-byte pages.  offset 0 -> page arg 0. */
	result = __syscall6(192, 0, tls_allocation_size,
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

static int
install_thread_pointer(void *thread_pointer)
{
	struct meuos_user_desc desc;
	long result;

	desc.entry_number = tls_entry_number;
	desc.base_addr = (unsigned int)(uintptr_t)thread_pointer;
	desc.limit = 0xFFFFF;
	/* seg_32bit(1) | limit_in_pages(0x10) | useable(0x40) */
	desc.flags = 0x51;
	result = __syscall6(243, (long)(uintptr_t)&desc, 0, 0, 0, 0, 0);
	if (__syscall_error(result))
		return -1;
	tls_entry_number = desc.entry_number;
	__meuos_load_gs((unsigned short)(desc.entry_number * 8 + 3));
	return 0;
}

void
__meuos_tls_init(char **environment)
{
	struct meuos_auxv32 *auxv;
	struct meuos_phdr32 *headers = 0;
	unsigned int entry_size = 0, count = 0, index;
	void *thread_pointer;

	while (*environment)
		++environment;
	auxv = (struct meuos_auxv32 *)(environment + 1);
	for (; auxv->type != AT_NULL; ++auxv) {
		if (auxv->type == AT_PHDR) headers = (struct meuos_phdr32 *)auxv->value;
		else if (auxv->type == AT_PHENT) entry_size = auxv->value;
		else if (auxv->type == AT_PHNUM) count = auxv->value;
	}
	if (!headers || entry_size != sizeof(*headers))
		return;
	for (index = 0; index < count; ++index) {
		struct meuos_phdr32 *header = (struct meuos_phdr32 *)((char *)headers + index * entry_size);
		if (header->p_type != PT_TLS)
			continue;
		if (!header->p_memsz || !header->p_align || (header->p_align & (header->p_align - 1)))
			return;
		tls_image = (const void *)(uintptr_t)header->p_vaddr;
		tls_file_size = (size_t)header->p_filesz;
		tls_memory_size = (size_t)header->p_memsz;
		tls_alignment = (size_t)header->p_align;
		thread_pointer = allocate_tls();
		if (!thread_pointer || install_thread_pointer(thread_pointer) < 0)
			return;
		return;
	}
}

void *
__meuos_tls_alloc(void) { return allocate_tls(); }
size_t __meuos_tls_size(void) { return tls_allocation_size; }

int
__meuos_set_tls(void *thread_pointer)
{
	return install_thread_pointer(thread_pointer);
}

/* 释放 variant II TLS 块：与 x86_64 相同的偏移公式。__syscall_number()
 * 在 i386 上把内部号 11 翻译成 munmap(91)。 */
#define LINUX_SYS_MUNMAP 11
void
__meuos_tls_free(void *thread_pointer)
{
	if (thread_pointer && tls_allocation_size)
		__syscall6(LINUX_SYS_MUNMAP,
		    (long)((char *)thread_pointer - tls_allocation_size + sizeof(void *)),
		    tls_allocation_size, 0, 0, 0, 0);
}
