/*
 * Copyright (C) 1997-2001 Id Software, Inc.
 * Copyright (C) 2026 the Quake2PS3 contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * =======================================================================
 *
 * Hunk allocator for the PS3. The unix version reserves address space with
 * mmap() and trims it with mremap(); neither exists here. The hunk is only
 * used by the renderers to hold one model each, with an exact size
 * estimate, so an allocation of the maximum is fine. The unused tail is
 * not given back (pointers into the hunk are already handed out); the
 * slack is small.
 *
 * Big hunks -- in practice the world model, 10-40 MB -- come straight from
 * lv2 (sysMemoryAllocate, 64 KB pages) instead of newlib's heap. That heap
 * is one linear range: a 39 MB block freed at a map change left a hole
 * that the next level's textures, sounds and entities settled in, so after
 * a dozen map changes and loads there were no 39 MB in one piece any more
 * and going back to waste1 died with "unable to allocate 39128039 bytes".
 * lv2 maps pages wherever they are, and takes them all back on free.
 * Small hunks (the alias and sprite models) stay in the heap.
 *
 * Every big hunk logs the memory left ([mem]), so a leak would show up
 * as a falling line from one map to the next.
 *
 * Layout: [size_t total][int from lv2][pad to 32][data...]
 *
 * =======================================================================
 */

#include <malloc.h>
#include <stdlib.h>
#include <string.h>

#ifdef __PPU__
#include <ppu-lv2.h>
#include <sys/memory.h>
#endif

#include "../../common/header/common.h"
#include "ps3_platform.h"

#define HUNK_HEADER 32               /* keeps the data cacheline aligned */
#define BIG_HUNK    (1024 * 1024)    /* from here on, straight from lv2 */
#define LV2_PAGE    (64 * 1024)

static byte *membase;
static size_t maxhunksize;
static size_t curhunksize;

#ifdef __PPU__
void PS3_BigAllocStats(unsigned *count, size_t *bytes);   /* bigalloc.c */

typedef struct
{
	u32 total;
	u32 available;
} ps3_meminfo_t;

/* sys_memory_get_user_memory_size: what the process may still take */
static s32
GetUserMemory(ps3_meminfo_t *info)
{
	lv2syscall1(352, (u64)info);
	return_to_user_prog(s32);
}
#endif

static void
LogMemory(const char *what, size_t size)
{
#ifdef __PPU__
	struct mallinfo mi = mallinfo();
	ps3_meminfo_t info;
	unsigned bigs;
	size_t bigbytes;

	memset(&info, 0, sizeof(info));
	GetUserMemory(&info);
	PS3_BigAllocStats(&bigs, &bigbytes);
	PS3_Log("[mem] %s %u KB: lv2 free %u KB of %u KB; heap %u KB, "
		"%u KB in use, %u KB free in it; %u big blocks, %u KB",
		what, (unsigned)(size / 1024), info.available / 1024,
		info.total / 1024, (unsigned)mi.arena / 1024,
		(unsigned)mi.uordblks / 1024, (unsigned)mi.fordblks / 1024,
		bigs, (unsigned)(bigbytes / 1024));
#else
	PS3_Log("[mem] %s %u KB", what, (unsigned)(size / 1024));
#endif
}

void *
Hunk_Begin(int maxsize)
{
	size_t total;
	int from_lv2 = 0;

	/* Same accounting as the unix hunk: the renderers' size estimates
	   rely on its slack (sizeof(size_t) + 32 on top of maxsize), and
	   Hunk_Alloc rounds every block up to 32. Without it: "Hunk_Alloc:
	   overflow 2624 > 2596" on the first map. */
	maxhunksize = (size_t)maxsize + sizeof(size_t) + 32;
	curhunksize = 0;
	total = maxhunksize + HUNK_HEADER;
	membase = NULL;

#ifdef __PPU__
	if (total >= BIG_HUNK)
	{
		sys_mem_addr_t addr = 0;

		total = (total + LV2_PAGE - 1) & ~(size_t)(LV2_PAGE - 1);
		if (sysMemoryAllocate(total, SYS_MEMORY_PAGE_SIZE_64K, &addr) == 0)
		{
			membase = (byte *)(uintptr_t)addr;
			from_lv2 = 1;
		}
		else
		{
			LogMemory("lv2 refused a hunk of", total);
			total = maxhunksize + HUNK_HEADER;
		}
	}
#endif

	if (membase == NULL)
	{
		membase = memalign(128, total);
	}

	if (membase == NULL)
	{
		LogMemory("no room for a hunk of", total);
		Sys_Error("unable to allocate %d bytes", maxsize);
	}

	memset(membase, 0, total);
	*((size_t *)membase) = total;
	*((int *)(membase + sizeof(size_t))) = from_lv2;

	if (total >= BIG_HUNK)
	{
		LogMemory(from_lv2 ? "hunk from lv2" : "hunk from the heap", total);
	}

	return membase + HUNK_HEADER;
}

void *
Hunk_Alloc(int size)
{
	byte *buf;

	/* round to cacheline */
	size = (size + 31) & ~31;

	if (curhunksize + size > maxhunksize)
	{
		Sys_Error("%s: overflow %d > %d",
			__func__, (int)(curhunksize + size), (int)maxhunksize);
	}

	buf = membase + HUNK_HEADER + curhunksize;
	curhunksize += size;

	return buf;
}

int
Hunk_End(void)
{
	return (int)curhunksize;
}

void
Hunk_Free(void *base)
{
	byte *block;

	if (!base)
	{
		return;
	}

	block = ((byte *)base) - HUNK_HEADER;

#ifdef __PPU__
	if (*((int *)(block + sizeof(size_t))))
	{
		sysMemoryFree((sys_mem_addr_t)(uintptr_t)block);
		return;
	}
#endif

	free(block);
}
