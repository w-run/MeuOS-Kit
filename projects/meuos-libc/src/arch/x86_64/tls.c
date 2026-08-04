#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include "../../internal/syscall.h"
#include "../../internal/tls.h"

/* x86_64 ELF TLS uses variant II: static TLS is immediately below %fs.
 * TCB layout (also see src/internal/tls.h):
 *   TP[0] (+0x00) = TP itself (self / pthread anchor)
 *   TP[1] (+0x08) = DTV pointer
 * The static main block lives at [TP - tls_memory_size, TP).  Dynamic
 * modules registered via __meuos_tls_add_module get per-thread blocks
 * laid out above the TCB, referenced through a per-thread DTV. */
#define AT_NULL 0
#define AT_PHDR 3
#define AT_PHENT 4
#define AT_PHNUM 5
#define PT_TLS 7
#define LINUX_SYS_MMAP 9
#define ARCH_SET_FS 0x1002
#define PTRSZ ((size_t)sizeof(void *))

struct meuos_auxv { unsigned long type; unsigned long value; };
struct meuos_phdr {
	uint32_t type, flags;
	uint64_t offset, virtual_address, physical_address;
	uint64_t file_size, memory_size, alignment;
};

extern long __meuos_arch_prctl(long, unsigned long);

/* --- module TLS registry (storage) --- */
struct tls_module __meuos_tls_modules[MEUOS_MAX_TLS_MODULES];
int __meuos_tls_module_count;

static const void *tls_image;
static size_t tls_file_size, tls_memory_size, tls_alignment, tls_allocation_size;

static size_t
round_up(size_t value, size_t alignment)
{
	return (value + alignment > 0 && alignment > 0)
	       ? (value + alignment - 1) & ~(alignment - 1)
	       : value;
}

/* Registry accessors, called by rtld on dlopen (via extern) and internally.
 * Insertion keeps modules sorted by modid so thread allocation order is
 * deterministic. */
void
__meuos_tls_add_module(long modid, const void *tpl, size_t filesz,
                       size_t memsz, size_t align)
{
	struct tls_module *cur;
	int i;

	if (modid <= 0 || memsz == 0)
		return;
	/* Replace an existing entry with the same modid (re-registration). */
	for (i = 0; i < __meuos_tls_module_count; ++i) {
		cur = &__meuos_tls_modules[i];
		if (cur->modid == modid) {
			cur->tpl = tpl;
			cur->filesz = filesz;
			cur->memsz = memsz;
			cur->align = align ? align : 1;
			return;
		}
	}
	if (__meuos_tls_module_count >= MEUOS_MAX_TLS_MODULES)
		return;
	cur = &__meuos_tls_modules[__meuos_tls_module_count];
	cur->modid = modid;
	cur->tpl = tpl;
	cur->filesz = filesz;
	cur->memsz = memsz;
	cur->align = align ? align : 1;
	__meuos_tls_module_count++;
}

struct tls_module *
__meuos_tls_lookup(long modid)
{
	int i;

	for (i = 0; i < __meuos_tls_module_count; ++i)
		if (__meuos_tls_modules[i].modid == modid)
			return &__meuos_tls_modules[i];
	return 0;
}

/* Allocate a chunk of anonymous memory. */
static void *
tls_mmap(size_t size)
{
	long r = __syscall6(LINUX_SYS_MMAP, 0, size,
	                     PROT_READ | PROT_WRITE,
	                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	return __syscall_error(r) ? 0 : (void *)r;
}

/* Build a thread's TLS area + DTV.
 *
 * Returns the thread pointer, or 0 on failure.  Layout of the single
 * contiguous mmap:
 *   [0 .. tls_end)                  static main block (module 1)
 *   [tls_end .. tls_end+TCB_SIZE)   TCB (self + DTV ptr)
 *   [tcb .. +dyn_total)             each registered dynamic module block
 *   [.. +dtv_size)                  DTV array (slots 0..max_modid)
 *
 * The static-only path (no registered dynamic modules) is identical to
 * before except for the extra DTV slot, which is left NULL. */
static void *
allocate_tls(void)
{
	size_t tls_end, tcb_off, dyn_off, dyn_total, dtv_off, dtv_size;
	size_t max_modid = 0, total;
	struct tls_module *mod;
	unsigned char *memory, *tp, *dtv;
	int i;

	/* Start with the static main block layout. */
	tls_end = tls_alignment ? round_up(tls_memory_size, tls_alignment)
	                        : tls_memory_size;
	tcb_off = tls_end;
	dyn_off = tls_end + MEUOS_TCB_SIZE;

	/* Size the dynamic module blocks + DTV from the registry. */
	dyn_total = 0;
	for (i = 0; i < __meuos_tls_module_count; ++i) {
		mod = &__meuos_tls_modules[i];
		if (mod->memsz == 0)
			continue;
		size_t align = mod->align > 16 ? mod->align : 16;
		dyn_total += (mod->memsz + align - 1) & ~(align - 1);
		if ((size_t)mod->modid > max_modid)
			max_modid = (size_t)mod->modid;
	}
	dtv_off = dyn_off + dyn_total;
	dtv_size = (max_modid + 1) * PTRSZ;   /* slot0 = generation, slot id = block */
	total = dtv_off + dtv_size;

	if (tls_memory_size == 0 && max_modid == 0)
		return 0;   /* no TLS at all */

	memory = tls_mmap(total);
	if (!memory)
		return 0;
	memset(memory, 0, total);
	tp = memory + tls_end;

	/* Copy the static main module's .tdata into [TP-memsz, TP). */
	if (tls_memory_size)
		memcpy(tp - tls_memory_size, tls_image, tls_file_size);

	/* Publish TCB self + DTV pointer. */
	*(void **)(tp + MEUOS_TCB_SELF_OFF) = tp;
	tls_allocation_size = total;
	if (max_modid == 0) {
		*(void **)(tp + MEUOS_TCB_DTV_OFF) = 0;   /* no DTV in static-only */
		return tp;
	}

	dtv = memory + dtv_off;
	((uintptr_t *)dtv)[0] = 1;   /* generation */
	/* Each dynamic module block gets laid out above the TCB and its
	 * address stored in DTV[modid]. */
	{
		size_t cursor = dyn_off;
		for (i = 0; i < __meuos_tls_module_count; ++i) {
			mod = &__meuos_tls_modules[i];
			if (mod->memsz == 0)
				continue;
			size_t align = mod->align > 16 ? mod->align : 16;
			size_t sz = (mod->memsz + align - 1) & ~(align - 1);
			unsigned char *blk = memory + cursor;
			if (mod->tpl && mod->filesz > 0)
				memcpy(blk, mod->tpl, mod->filesz);
			((uintptr_t *)dtv)[mod->modid] = (uintptr_t)blk;
			cursor += sz;
		}
	}
	*(void **)(tp + MEUOS_TCB_DTV_OFF) = dtv;
	return tp;
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
		/* Prefer the main program's PT_TLS as the static main block.  A
		 * zero-size/zero-align segment is ignored (no static TLS). */
		if (header->memory_size < 1)
			continue;
		if (header->alignment == 0 ||
		    (header->alignment & (header->alignment - 1)))
			continue;
		tls_image = (const void *)(uintptr_t)header->virtual_address;
		tls_file_size = (size_t)header->file_size;
		tls_memory_size = (size_t)header->memory_size;
		tls_alignment = (size_t)header->alignment;
		thread_pointer = allocate_tls();
		if (!thread_pointer || __meuos_arch_prctl(ARCH_SET_FS, (unsigned long)thread_pointer) < 0)
			return;
		return;
	}
	/* Still need a TP for threads even if the main program has no static
	 * TLS (module 1 absent) but registered dynamic modules exist. */
	if (__meuos_tls_module_count > 0) {
		thread_pointer = allocate_tls();
		if (thread_pointer &&
		    __meuos_arch_prctl(ARCH_SET_FS, (unsigned long)thread_pointer) >= 0)
			return;
	}
}

void *
__meuos_tls_alloc(void) { return allocate_tls(); }
size_t __meuos_tls_size(void) { return tls_allocation_size; }

/* 释放 variant II TLS 块：mmap base = TP - tls_memsz/末端到达区；因为
 * TCB 在主块之上、动态块与 DTV 都在同一 mmap 内，所以整体 munmap 一次：
 * base = TP - tls_end（主块起点），size = tls_allocation_size。 */
#define LINUX_SYS_MUNMAP 11
void
__meuos_tls_free(void *thread_pointer)
{
	if (thread_pointer && tls_allocation_size) {
		size_t tls_end = tls_alignment
		    ? round_up(tls_memory_size, tls_alignment)
		    : tls_memory_size;
		__syscall6(LINUX_SYS_MUNMAP,
		    (long)((char *)thread_pointer - tls_end),
		    tls_allocation_size, 0, 0, 0, 0);
	}
}
