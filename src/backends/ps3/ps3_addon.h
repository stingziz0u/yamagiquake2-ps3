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
 * Included ahead of every file of the mission packs (ps3/Makefile,
 * "-include"), which are built from Yamagi's own repositories without any
 * change.
 *
 * PSL1GHT has no chdir() and its fopen()/stat() only take absolute paths.
 * baseq2 gets that from the engine's patched shared.c (Q_fopen resolves
 * through PS3_AbsPath); the mission packs carry their own copy of
 * shared.c, so here fopen() and stat() are routed through PS3_AbsPath()
 * instead: relative names resolve against the emulated working directory
 * (src/backends/ps3/system.c), absolute ones are left alone.
 *
 * =======================================================================
 */

#ifndef PS3_ADDON_H
#define PS3_ADDON_H

#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>

const char *PS3_AbsPath(const char *path, char *out, size_t size);

static inline FILE *
PS3_AddonFopen(const char *path, const char *mode)
{
	char abspath[1024];

	return fopen(PS3_AbsPath(path, abspath, sizeof(abspath)), mode);
}

static inline int
PS3_AddonStat(const char *path, struct stat *buf)
{
	char abspath[1024];

	return stat(PS3_AbsPath(path, abspath, sizeof(abspath)), buf);
}

/* function-like: "struct stat" and the declarations above are untouched */
#define fopen(path, mode) PS3_AddonFopen((path), (mode))
#define stat(path, buf) PS3_AddonStat((path), (buf))

#endif
