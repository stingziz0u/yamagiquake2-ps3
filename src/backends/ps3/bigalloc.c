/*
 * Copyright (C) 2026 the Quake2PS3 contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * =======================================================================
 *
 * Big allocations straight from lv2.
 *
 * newlib's heap is one linear range that only gives memory back to lv2
 * from its top. Every map loads and frees blocks of hundreds of KB to a
 * few MB (the BSP read from the pak, image conversions, sound caches);
 * the small blocks allocated meanwhile stay between them, so the heap
 * kept growing (56 -> 91 MB over one pass of the game) while holding
 * 30 MB it could not return. With the world model's 38 MB hunk on top,
 * waste1 no longer fit: "unable to allocate 39128039 bytes" with 37 MB
 * free in lv2 and 33 MB free inside the heap.
 *
 * The link wraps malloc, calloc, realloc and free (ps3/Makefile,
 * -Wl,--wrap=...). Blocks of BIG_ALLOC or more come from lv2 in 64 KB
 * pages (sysMemoryAllocate) and go back whole on free, wherever they
 * are; the heap keeps only small blocks. If lv2 says no, the heap is
 * used as before. newlib's own internal allocations (_malloc_r: stdio
 * buffers, strdup) are not wrapped and are all small.
 *
 * A big block's pointer is its first page + OFFSET, and a table of them
 * tells them apart on free: a heap pointer is only looked up when its
 * low 16 bits happen to be OFFSET, and never read.
 *
 * Only on the console: the host test harness links the plain allocator.
 *
 * =======================================================================
 */

#ifdef __PPU__

#include <malloc.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <sys/memory.h>
#include <sys/mutex.h>

#define BIG_ALLOC (256 * 1024)
#define PAGE      (64 * 1024)
#define OFFSET    128                 /* keeps malloc's alignment and more */
#define SLOTS     4096                /* power of two */
#define EMPTY     ((uintptr_t)0)
#define REMOVED   ((uintptr_t)1)

void *__real_malloc(size_t size);
void *__real_calloc(size_t n, size_t size);
void *__real_realloc(void *p, size_t size);
void __real_free(void *p);

void *__wrap_malloc(size_t size);
void *__wrap_calloc(size_t n, size_t size);
void *__wrap_realloc(void *p, size_t size);
void __wrap_free(void *p);

typedef struct
{
	uintptr_t ptr;      /* what the caller got */
	size_t size;        /* what it asked for */
	size_t room;        /* what fits before the end of its pages */
} big_t;

static big_t big[SLOTS];
static sys_mutex_t big_mutex;
static int big_ready;
static unsigned big_count;
static size_t big_bytes;

/* malloc can run before this (other constructors): until then it all
   goes to the heap. */
static void __attribute__((constructor))
BigInit(void)
{
	sys_mutex_attr_t attr;

	sysMutexAttrInitialize(attr);
	big_ready = (sysMutexCreate(&big_mutex, &attr) == 0);
}

static unsigned
Hash(uintptr_t p)
{
	return (unsigned)(p >> 16) & (SLOTS - 1);
}

/* under the lock */
static int
Find(uintptr_t p)
{
	unsigned i = Hash(p), n;

	for (n = 0; n < SLOTS; n++, i = (i + 1) & (SLOTS - 1))
	{
		if (big[i].ptr == p)
		{
			return (int)i;
		}
		if (big[i].ptr == EMPTY)
		{
			return -1;
		}
	}
	return -1;
}

/* under the lock */
static int
Insert(uintptr_t p, size_t size, size_t room)
{
	unsigned i = Hash(p), n;

	for (n = 0; n < SLOTS; n++, i = (i + 1) & (SLOTS - 1))
	{
		if (big[i].ptr == EMPTY || big[i].ptr == REMOVED)
		{
			big[i].ptr = p;
			big[i].size = size;
			big[i].room = room;
			big_count++;
			big_bytes += room + OFFSET;
			return 1;
		}
	}
	return 0;
}

static void *
BigAlloc(size_t size)
{
	size_t total;
	sys_mem_addr_t addr = 0;
	uintptr_t p;
	int ok;

	if (!big_ready || size > 0x7fffffff - OFFSET - PAGE)
	{
		return NULL;
	}

	total = (size + OFFSET + PAGE - 1) & ~(size_t)(PAGE - 1);
	if (sysMemoryAllocate(total, SYS_MEMORY_PAGE_SIZE_64K, &addr) != 0)
	{
		return NULL;
	}
	p = (uintptr_t)addr + OFFSET;

	sysMutexLock(big_mutex, 0);
	ok = Insert(p, size, total - OFFSET);
	sysMutexUnlock(big_mutex);

	if (!ok)
	{
		sysMemoryFree(addr);    /* table full: the heap it is */
		return NULL;
	}
	return (void *)p;
}

/* Whether p is a big block; its size and room if so. */
static int
IsBig(void *p, size_t *size, size_t *room, int remove)
{
	uintptr_t u = (uintptr_t)p;
	int i;

	if (!p || !big_ready || (u & (PAGE - 1)) != OFFSET)
	{
		return 0;
	}

	sysMutexLock(big_mutex, 0);
	i = Find(u);
	if (i >= 0)
	{
		if (size)
		{
			*size = big[i].size;
		}
		if (room)
		{
			*room = big[i].room;
		}
		if (remove)
		{
			big[i].ptr = REMOVED;
			big_count--;
			big_bytes -= big[i].room + OFFSET;
		}
	}
	sysMutexUnlock(big_mutex);

	return i >= 0;
}

void *
__wrap_malloc(size_t size)
{
	if (size >= BIG_ALLOC)
	{
		void *p = BigAlloc(size);

		if (p)
		{
			return p;
		}
	}
	return __real_malloc(size);
}

void *
__wrap_calloc(size_t n, size_t size)
{
	size_t total = n * size;

	if (size != 0 && total / size != n)
	{
		return NULL;    /* overflow */
	}
	if (total >= BIG_ALLOC)
	{
		void *p = BigAlloc(total);

		if (p)
		{
			memset(p, 0, total);
			return p;
		}
	}
	return __real_calloc(n, size);
}

void
__wrap_free(void *p)
{
	if (IsBig(p, NULL, NULL, 1))
	{
		sysMemoryFree((sys_mem_addr_t)((uintptr_t)p - OFFSET));
		return;
	}
	__real_free(p);
}

void *
__wrap_realloc(void *p, size_t size)
{
	size_t old, room;
	void *q;

	if (!p)
	{
		return __wrap_malloc(size);
	}
	if (size == 0)
	{
		__wrap_free(p);
		return NULL;
	}

	if (IsBig(p, &old, &room, 0))
	{
		if (size <= room && size >= BIG_ALLOC / 2)
		{
			/* still fits its pages: stays, with the new size */
			sysMutexLock(big_mutex, 0);
			big[Find((uintptr_t)p)].size = size;
			sysMutexUnlock(big_mutex);
			return p;
		}
		q = __wrap_malloc(size);
		if (!q)
		{
			return NULL;
		}
		memcpy(q, p, old < size ? old : size);
		__wrap_free(p);
		return q;
	}

	if (size >= BIG_ALLOC)
	{
		/* a heap block growing big: move it out */
		q = BigAlloc(size);
		if (q)
		{
			old = malloc_usable_size(p);
			memcpy(q, p, old < size ? old : size);
			__real_free(p);
			return q;
		}
	}
	return __real_realloc(p, size);
}

/* For the [mem] lines (hunk.c). */
void
PS3_BigAllocStats(unsigned *count, size_t *bytes)
{
	if (big_ready)
	{
		sysMutexLock(big_mutex, 0);
	}
	*count = big_count;
	*bytes = big_bytes;
	if (big_ready)
	{
		sysMutexUnlock(big_mutex);
	}
}

#endif /* __PPU__ */
