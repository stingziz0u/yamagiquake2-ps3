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
 * estimate, so an aligned malloc() of the maximum is fine. The unused tail
 * is not given back (pointers into the hunk are already handed out, and an
 * aligned block can't be shrunk in place portably); the slack is small.
 *
 * Layout: [size_t total][pad to 32][data...]
 *
 * =======================================================================
 */

#include <malloc.h>
#include <stdlib.h>
#include <string.h>

#include "../../common/header/common.h"

#define HUNK_HEADER 32   /* keeps the data cacheline aligned */

static byte *membase;
static size_t maxhunksize;
static size_t curhunksize;

void *
Hunk_Begin(int maxsize)
{
	/* Same accounting as the unix hunk: the renderers' size estimates
	   rely on its slack (sizeof(size_t) + 32 on top of maxsize), and
	   Hunk_Alloc rounds every block up to 32. Without it: "Hunk_Alloc:
	   overflow 2624 > 2596" on the first map. */
	maxhunksize = (size_t)maxsize + sizeof(size_t) + 32;
	curhunksize = 0;

	membase = memalign(128, maxhunksize + HUNK_HEADER);

	if (membase == NULL)
	{
		Sys_Error("unable to allocate %d bytes", maxsize);
	}

	memset(membase, 0, maxhunksize + HUNK_HEADER);
	*((size_t *)membase) = maxhunksize + HUNK_HEADER;

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
	if (base)
	{
		free(((byte *)base) - HUNK_HEADER);
	}
}
