#include <stdint.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * Deliberately small first-fit allocator for bootstrap use.  It owns only
 * memory obtained through sbrk, so it neither calls nor depends on a host
 * allocator.  Its metadata and program-break updates are protected by a
 * single C11 atomic lock.  This mirrors the small-bootstrap allocator lock
 * discipline used by established libcs while keeping this implementation
 * independent and auditable.
 */
/* struct 总大小必须为 16 的倍数（max_align_t 对齐要求）：
 * 否则每次 sbrk 增量 sizeof(*block)+size 非 16 倍数，block+1（用户区）
 * 会落在 8 字节对齐地址上，__int128/long double 等类型将未对齐。
 * reserved 显式填充 24 -> 32 字节。 */
struct allocation {
	size_t size;
	int available;
	size_t reserved;
	struct allocation *next;
};

static struct allocation *allocation_head;
static atomic_flag allocation_lock = ATOMIC_FLAG_INIT;

static void
lock_allocator(void)
{
	while (atomic_flag_test_and_set_explicit(&allocation_lock,
		memory_order_acquire))
		;
}

static void
unlock_allocator(void)
{
	atomic_flag_clear_explicit(&allocation_lock, memory_order_release);
}

static size_t
aligned_size(size_t size)
{
	const size_t alignment = 16;

	if (size == 0)
		size = 1;
	if (size > SIZE_MAX - (alignment - 1))
		return 0;
	return (size + alignment - 1) & ~(alignment - 1);
}

static struct allocation *
find_available(size_t size)
{
	struct allocation *block;

	for (block = allocation_head; block; block = block->next)
		if (block->available && block->size >= size)
			return block;
	return 0;
}

void *
malloc(size_t requested_size)
{
	struct allocation *block;
	size_t size = aligned_size(requested_size);

	if (!size || size > SIZE_MAX - sizeof(*block))
		return 0;
	lock_allocator();
	block = find_available(size);
	if (block) {
		block->available = 0;
		unlock_allocator();
		return block + 1;
	}
	block = sbrk((intptr_t)(sizeof(*block) + size));
	if (block == (void *)-1) {
		unlock_allocator();
		return 0;
	}
	block->size = size;
	block->available = 0;
	block->next = allocation_head;
	allocation_head = block;
	unlock_allocator();
	return block + 1;
}

void
free(void *pointer)
{
	struct allocation *block;

	if (!pointer)
		return;
	block = (struct allocation *)pointer - 1;
	lock_allocator();
	block->available = 1;
	unlock_allocator();
}

void *
calloc(size_t count, size_t size)
{
	void *pointer;

	if (size && count > SIZE_MAX / size)
		return 0;
	pointer = malloc(count * size);
	if (pointer)
		memset(pointer, 0, count * size);
	return pointer;
}

void *
realloc(void *pointer, size_t size)
{
	struct allocation *block;
	void *replacement;
	size_t old_size;

	if (!pointer)
		return malloc(size);
	if (size == 0) {
		free(pointer);
		return 0;
	}
	block = (struct allocation *)pointer - 1;
	lock_allocator();
	old_size = block->size;
	if (size <= old_size) {
		unlock_allocator();
		return pointer;
	}
	unlock_allocator();
	replacement = malloc(size);
	if (!replacement)
		return 0;
	lock_allocator();
	memcpy(replacement, pointer, old_size);
	/* Mark old block as available (manual free — free() would deadlock) */
	block = (struct allocation *)pointer - 1;
	block->available = 1;
	unlock_allocator();
	return replacement;
}
