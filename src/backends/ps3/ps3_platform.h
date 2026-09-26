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
 * PS3 (PSL1GHT) platform layer -- shared declarations.
 *
 * Everything that talks to PSL1GHT lives behind __PPU__. Without it the
 * same files build against plain POSIX, which is what the host harness
 * (ps3/hosttest, powerpc64 big-endian under qemu) uses.
 *
 * =======================================================================
 */

#ifndef PS3_PLATFORM_H
#define PS3_PLATFORM_H

#include <stddef.h>

#ifndef PS3_APPID
#define PS3_APPID "QUAKE2PS3"
#endif

/* Game data (pak files, mission pack dirs) is uploaded here by FTP. It is
 * wiped when the PKG is uninstalled from the XMB. */
#ifndef PS3_USRDIR
#define PS3_USRDIR "/dev_hdd0/game/" PS3_APPID "/USRDIR"
#endif

/* Config and savegames: survives reinstalling the PKG. */
#ifndef PS3_DATADIR
#define PS3_DATADIR "/dev_hdd0/data/quake2"
#endif

#define PS3_LOGFILE    PS3_USRDIR "/quake2_log.txt"
#define PS3_LOGFILE_OLD PS3_USRDIR "/quake2_log.old.txt"

/* ps3_log.c */
void PS3_LogInit(void);
void PS3_Log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void PS3_LogRaw(const char *text);   /* as is, no newline added */
void PS3_LogShutdown(void);

/* system.c */
void PS3_Pump(void);                  /* sysutil queue, once per frame */
int  PS3_ExitRequested(void);
void PS3_SetExitRequested(void);
/* Relative path -> absolute, against the emulated working directory.
 * PSL1GHT's fopen() only understands absolute paths, and there is no
 * chdir(): sv_save.c changes directory and then opens "base1.sav". */
const char *PS3_AbsPath(const char *path, char *out, size_t size);

#endif
