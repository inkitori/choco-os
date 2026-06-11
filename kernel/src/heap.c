// Kernel heap backed by the PMM. At init we claim the largest contiguous
// run of physical pages we can get (up to HEAP_MAX) and run a first-fit
// free-list allocator with coalescing over it, addressed through the HHDM.
#include "malloc.h"
#include "pmm.h"
#include "kprintf.h"
#include "string.h"

#include <stdint.h>
#include <stdbool.h>

#define HEAP_MAX (768ull * 1024 * 1024)
#define HEAP_MIN (16ull * 1024 * 1024)
#define ALIGNMENT 16ull

#include "limine.h"

extern volatile struct limine_hhdm_request hhdm_request;

typedef struct Block
{
	size_t size; // payload size, excluding header
	bool free;
	struct Block *next; // next block by address
	struct Block *prev; // previous block by address
} Block;

#define HDR_SIZE ((sizeof(Block) + ALIGNMENT - 1) & ~(ALIGNMENT - 1))

static Block *heap_head = NULL;
static size_t total_size = 0;
static size_t used_size = 0;

void heap_init(void)
{
	uint64_t hhdm = hhdm_request.response->offset;

	size_t want = HEAP_MAX;
	void *phys = NULL;
	while (want >= HEAP_MIN)
	{
		phys = pmm_alloc_pages(want / PAGE_SIZE);
		if (phys)
			break;
		want /= 2;
	}
	if (!phys)
	{
		kprintf("heap: FATAL could not allocate heap region\n");
		return;
	}

	heap_head = (Block *)((uint64_t)phys + hhdm);
	heap_head->size = want - HDR_SIZE;
	heap_head->free = true;
	heap_head->next = NULL;
	heap_head->prev = NULL;

	total_size = want;
	used_size = 0;
	kprintf("heap: %lu MiB at phys %p\n", (uint64_t)(want / (1024 * 1024)), phys);
}

void *malloc(size_t size)
{
	if (size == 0 || heap_head == NULL)
		return NULL;

	size = (size + ALIGNMENT - 1) & ~(ALIGNMENT - 1);

	for (Block *b = heap_head; b; b = b->next)
	{
		if (!b->free || b->size < size)
			continue;

		// Split if the remainder can hold a header plus minimal payload.
		if (b->size >= size + HDR_SIZE + ALIGNMENT)
		{
			Block *split = (Block *)((uint8_t *)b + HDR_SIZE + size);
			split->size = b->size - size - HDR_SIZE;
			split->free = true;
			split->next = b->next;
			split->prev = b;
			if (b->next)
				b->next->prev = split;
			b->next = split;
			b->size = size;
		}
		b->free = false;
		used_size += b->size + HDR_SIZE;
		return (uint8_t *)b + HDR_SIZE;
	}

	kprintf("malloc: out of memory for %lu bytes\n", (uint64_t)size);
	return NULL;
}

static void coalesce_with_next(Block *b)
{
	Block *n = b->next;
	if (n && n->free && b->free)
	{
		b->size += HDR_SIZE + n->size;
		b->next = n->next;
		if (n->next)
			n->next->prev = b;
	}
}

void free(void *ptr)
{
	if (!ptr)
		return;

	Block *b = (Block *)((uint8_t *)ptr - HDR_SIZE);
	if (b->free)
	{
		kprintf("free: double free at %p\n", ptr);
		return;
	}
	b->free = true;
	used_size -= b->size + HDR_SIZE;

	coalesce_with_next(b);
	if (b->prev && b->prev->free)
		coalesce_with_next(b->prev);
}

void *calloc(size_t num, size_t size)
{
	if (num && size > (size_t)-1 / num)
		return NULL;
	size_t total = num * size;
	void *p = malloc(total);
	if (p)
		memset(p, 0, total);
	return p;
}

void *realloc(void *ptr, size_t size)
{
	if (!ptr)
		return malloc(size);
	if (size == 0)
	{
		free(ptr);
		return NULL;
	}

	Block *b = (Block *)((uint8_t *)ptr - HDR_SIZE);
	if (b->size >= size)
		return ptr;

	void *np = malloc(size);
	if (np)
	{
		memcpy(np, ptr, b->size);
		free(ptr);
	}
	return np;
}

size_t heap_total(void) { return total_size; }
size_t heap_used(void) { return used_size; }

#include "term.h"

void test_malloc(void)
{
	term_printf("heap: %lu MiB total, %lu KiB used\n",
				(uint64_t)(heap_total() / (1024 * 1024)),
				(uint64_t)(heap_used() / 1024));

	uint8_t *p1 = malloc(128);
	for (int i = 0; i < 128; i++)
		p1[i] = (uint8_t)i;
	bool ok = true;
	for (int i = 0; i < 128; i++)
		if (p1[i] != (uint8_t)i)
			ok = false;
	term_printf("1. malloc+write: %s\n", ok ? "PASS" : "FAIL");

	uint8_t *p2 = calloc(10, 10);
	ok = p2 != NULL;
	for (int i = 0; ok && i < 100; i++)
		if (p2[i] != 0)
			ok = false;
	term_printf("2. calloc zeroing: %s\n", ok ? "PASS" : "FAIL");

	uint8_t *p3 = realloc(p1, 256);
	ok = p3 != NULL;
	for (int i = 0; ok && i < 128; i++)
		if (p3[i] != (uint8_t)i)
			ok = false;
	term_printf("3. realloc: %s\n", ok ? "PASS" : "FAIL");

	// Big allocation for LLM-sized buffers.
	void *big = malloc(64ull * 1024 * 1024);
	term_printf("4. 64 MiB alloc: %s\n", big ? "PASS" : "FAIL");

	free(p2);
	free(p3);
	free(big);
	term_printf("heap after tests: %lu KiB used\n", (uint64_t)(heap_used() / 1024));
}
