/*
 * Copyright (C) 1997-2001 Id Software, Inc.
 * Copyright (C) 2026 the Quake2PS3 contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * =======================================================================
 *
 * System dependent functions for the PS3, modeled on
 * src/backends/unix/system.c. Differences:
 *
 *  - The games are linked statically (there is no dlopen on the PS3):
 *    Sys_GetGameAPI() picks baseq2's GetGameAPI(), or a mission pack's
 *    or mod's (renamed GetGameAPI_xatrix / _rogue / _zaero, see
 *    ps3/Makefile) after the "game" cvar.
 *  - There is no chdir()/getcwd()/realpath(). The working directory is
 *    emulated (sv_save.c changes into save/current and opens relative
 *    names), and Q_fopen() resolves relative names through PS3_AbsPath().
 *  - Directories are detected with opendir(): it is what is known to work
 *    on /dev_hdd0 (CrispyCell's launcher relies on it).
 *  - Console output goes to the log file.
 *
 * =======================================================================
 */

#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef __PPU__
#include <lv2/systime.h>
#include <sysutil/sysutil.h>
#endif

#include "../../common/header/common.h"
#include "../../common/header/glob.h"
#include "ps3_platform.h"

#ifndef DEDICATED_ONLY
#include "ps3_video.h"
#include "ps3_pad.h"
#endif

/* frame.c: set by signal handlers on unix, by the XMB "Quit Game" here. */
extern qboolean quitnextframe;

/* The game "DLL", linked in (see ps3/Makefile: game.o). */
void *GetGameAPI(void *import);

#ifdef PS3_ADDONS
void *GetGameAPI_xatrix(void *import);
void *GetGameAPI_rogue(void *import);
#endif

#ifdef PS3_ZAERO
/* only in personal builds (ps3/Makefile ZAERO=1): Zaero's code is not GPL */
void *GetGameAPI_zaero(void *import);
#endif

static char workdir[MAX_OSPATH] = PS3_USRDIR;

static volatile int exit_requested;

/* ================================================================ */

void
PS3_SetExitRequested(void)
{
	exit_requested = 1;
	quitnextframe = true;
}

int
PS3_ExitRequested(void)
{
	return exit_requested;
}

void
PS3_Pump(void)
{
#ifdef __PPU__
	/* Registering the callback is not enough: nothing is delivered unless
	   someone pumps the queue. Without it "Quit Game" in the XMB never
	   reaches us and the console reboots on exit. */
	sysUtilCheckCallback();
#endif
}

/* ================================================================ */

/* Strips trailing slashes, the PS3 file system calls dislike them. */
static void
StripSlashes(const char *in, char *out, size_t size)
{
	size_t len;

	Q_strlcpy(out, in, size);
	len = strlen(out);

	while (len > 1 && out[len - 1] == '/')
	{
		out[--len] = '\0';
	}
}

/* Builds an absolute path and folds "." and ".." components. Does not
   touch the file system. */
static void
MakeAbsolute(const char *in, char *out, size_t size)
{
	char tmp[MAX_OSPATH * 2];
	char *parts[128];
	int nparts = 0;
	char *p, *save = NULL;
	int i;

	if (in[0] == '/')
	{
		Q_strlcpy(tmp, in, sizeof(tmp));
	}
	else
	{
		Com_sprintf(tmp, sizeof(tmp), "%s/%s", workdir, in);
	}

	for (p = strtok_r(tmp, "/", &save); p; p = strtok_r(NULL, "/", &save))
	{
		if (!strcmp(p, "") || !strcmp(p, "."))
		{
			continue;
		}

		if (!strcmp(p, ".."))
		{
			if (nparts > 0)
			{
				nparts--;
			}

			continue;
		}

		if (nparts < (int)(sizeof(parts) / sizeof(parts[0])))
		{
			parts[nparts++] = p;
		}
	}

	if (nparts == 0)
	{
		Q_strlcpy(out, "/", size);
		return;
	}

	out[0] = '\0';

	for (i = 0; i < nparts; i++)
	{
		Q_strlcat(out, "/", (int)size);
		Q_strlcat(out, parts[i], (int)size);
	}
}

const char *
PS3_AbsPath(const char *path, char *out, size_t size)
{
	if (!path)
	{
		return path;
	}

	if (path[0] == '/')
	{
		return path;
	}

	MakeAbsolute(path, out, size);

	return out;
}

/* ================================================================ */

#ifndef DEDICATED_ONLY
/* A fatal error used to be a black screen and a trip back to the XMB
   (handoff notes 3.17). Show it, wait for X (or 20 seconds, or "Quit
   Game" in the XMB), then leave. */
static void
ShowErrorScreen(const char *error)
{
	char wrapped[8][64];
	const char *lines[14];
	int nlines = 0, n = 0, i;
	long long start;
	unsigned prev = 0xffff;

	if (!PS3_Video_Ready())
	{
		return;
	}

	lines[nlines++] = "Quake II stopped with an error:";
	lines[nlines++] = "";

	/* wrap the message at 56 columns */
	while (*error && n < 8)
	{
		int len = (int)strlen(error);
		int take = len;

		if (len > 56)
		{
			take = 56;

			for (i = 56; i > 30; i--)
			{
				if (error[i] == ' ')
				{
					take = i;
					break;
				}
			}
		}

		memcpy(wrapped[n], error, take);
		wrapped[n][take] = '\0';

		for (i = 0; i < take; i++)
		{
			if (wrapped[n][i] == '\n' || wrapped[n][i] == '\r')
			{
				wrapped[n][i] = ' ';
			}
		}

		lines[nlines++] = wrapped[n++];
		error += take;

		while (*error == ' ' || *error == '\n')
		{
			error++;
		}
	}

	lines[nlines++] = "";
	lines[nlines++] = "Details: USRDIR/quake2_log.txt";
	lines[nlines++] = "Press X to go back to the XMB.";

	PS3_Video_TextScreen("QUAKE II", lines, nlines);

	/* the client may already have shut the pad down (Com_Error runs
	   CL_Shutdown before Sys_Error) */
	PS3_Pad_Init();

	start = Sys_Microseconds();

	while (Sys_Microseconds() - start < 20000000ll && !PS3_ExitRequested())
	{
		ps3_padstate_t st;

		PS3_Pump();
		PS3_Pad_Poll(&st);

		/* first everything up (X may still be held from the game) */
		if ((st.buttons & PS3_PAD_CROSS) && !(prev & PS3_PAD_CROSS))
		{
			break;
		}

		prev = st.buttons;
		usleep(30000);
	}
}

/* The RSX must be released before exit(), flip handler first, or the
   console hard-reboots (handoff notes 3.8/3.9). */
static void
ShutdownPlatform(void)
{
	PS3_Video_Shutdown();
}
#endif

void
Sys_Error(const char *error, ...)
{
	static int in_error;
	va_list argptr;
	char string[1024];

	va_start(argptr, error);
	vsnprintf(string, sizeof(string), error, argptr);
	va_end(argptr);

	PS3_Log("=== Sys_Error: %s", string);

	if (in_error++)
	{
		/* failed again while shutting down: just get out */
#ifndef DEDICATED_ONLY
		ShutdownPlatform();
#endif
		PS3_LogShutdown();
		exit(1);
	}

#ifndef DEDICATED_ONLY
	ShowErrorScreen(string);
	CL_Shutdown();
#endif
	Qcommon_Shutdown();

#ifndef DEDICATED_ONLY
	ShutdownPlatform();
#endif
	PS3_LogShutdown();
	exit(1);
}

void
Sys_Quit(void)
{
	PS3_Log("Sys_Quit");

#ifndef DEDICATED_ONLY
	CL_Shutdown();
#endif
	Qcommon_Shutdown();

#ifndef DEDICATED_ONLY
	ShutdownPlatform();
#endif
	PS3_LogShutdown();
	exit(0);
}

void
Sys_Init(void)
{
	PS3_Log("Sys_Init: workdir %s", workdir);
}

/* ================================================================ */

#ifdef PS3_STAGE1
char *PS3_Stage1_ConsoleInput(void);   /* main.c */
#endif

char *
Sys_ConsoleInput(void)
{
	PS3_Pump();

#ifdef PS3_STAGE1
	return PS3_Stage1_ConsoleInput();
#else
	return NULL;
#endif
}

void
Sys_ConsoleOutput(char *string)
{
	/* 0x01 / 0x02 are color markers for terminals. */
	if ((string[0] == 0x01) || (string[0] == 0x02))
	{
		string++;
	}

	PS3_LogRaw(string);
}

/* ================================================================ */

long long
Sys_Microseconds(void)
{
	static long long first;
	long long now;

#ifdef __PPU__
	now = (long long)sysGetSystemTime();
#else
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	now = (long long)ts.tv_sec * 1000000ll + ts.tv_nsec / 1000;
#endif

	/* Set back first by 1ms so neither this function nor
	   Sys_Milliseconds() ever returns 0 (same as unix). */
	if (first == 0)
	{
		first = now - 1000;
	}

	return now - first;
}

int
Sys_Milliseconds(void)
{
	return (int)(Sys_Microseconds() / 1000ll);
}

void
Sys_Nanosleep(int nanosec)
{
	int usec = nanosec / 1000;

	usleep(usec > 0 ? usec : 1);
}

/* ================================================================ */

static char findbase[MAX_OSPATH];
static char findpath[MAX_OSPATH];
static char findpattern[MAX_OSPATH];
static DIR *fdir;

char *
Sys_FindFirst(const char *path, unsigned musthave, unsigned canhave)
{
	char abspath[MAX_OSPATH];
	struct dirent *d;
	char *p;

	if (fdir)
	{
		Sys_Error("Sys_BeginFind without close");
	}

	Q_strlcpy(findbase, PS3_AbsPath(path, abspath, sizeof(abspath)),
			sizeof(findbase));

	if ((p = strrchr(findbase, '/')) != NULL)
	{
		*p = 0;
		Q_strlcpy(findpattern, p + 1, sizeof(findpattern));
	}
	else
	{
		strcpy(findpattern, "*");
	}

	if (strcmp(findpattern, "*.*") == 0)
	{
		strcpy(findpattern, "*");
	}

	if ((fdir = opendir(findbase[0] ? findbase : "/")) == NULL)
	{
		return NULL;
	}

	while ((d = readdir(fdir)) != NULL)
	{
		if (!*findpattern || glob_match(findpattern, d->d_name))
		{
			if ((strcmp(d->d_name, ".") != 0) && (strcmp(d->d_name, "..") != 0))
			{
				Com_sprintf(findpath, sizeof(findpath), "%s/%s", findbase,
					d->d_name);
				return findpath;
			}
		}
	}

	return NULL;
}

char *
Sys_FindNext(unsigned musthave, unsigned canhave)
{
	struct dirent *d;

	if (fdir == NULL)
	{
		return NULL;
	}

	while ((d = readdir(fdir)) != NULL)
	{
		if (!*findpattern || glob_match(findpattern, d->d_name))
		{
			if ((strcmp(d->d_name, ".") != 0) && (strcmp(d->d_name, "..") != 0))
			{
				Com_sprintf(findpath, sizeof(findpath), "%s/%s", findbase, d->d_name);
				return findpath;
			}
		}
	}

	return NULL;
}

void
Sys_FindClose(void)
{
	if (fdir != NULL)
	{
		closedir(fdir);
	}

	fdir = NULL;
}

/* ================================================================ */

void
Sys_UnloadGame(void)
{
	/* Linked in, nothing to unload. */
}

void *
Sys_GetGameAPI(void *parms)
{
#ifdef PS3_ADDONS
	const char *game = Cvar_VariableString("game");

	if (Q_stricmp(game, "xatrix") == 0)
	{
		Com_Printf("Loading game: linked in (xatrix, The Reckoning)\n");
		return GetGameAPI_xatrix(parms);
	}

	if (Q_stricmp(game, "rogue") == 0)
	{
		Com_Printf("Loading game: linked in (rogue, Ground Zero)\n");
		return GetGameAPI_rogue(parms);
	}

#ifdef PS3_ZAERO
	if (Q_stricmp(game, "zaero") == 0)
	{
		Com_Printf("Loading game: linked in (zaero)\n");
		return GetGameAPI_zaero(parms);
	}
#endif
#endif

	/* baseq2, and any mod that is only data (maps, models...): a mod
	   with its own game code runs only if it is linked in above */
	Com_Printf("Loading game: linked in (baseq2)\n");

	return GetGameAPI(parms);
}

/* ================================================================ */

qboolean
Sys_IsDir(const char *path)
{
	char abspath[MAX_OSPATH], clean[MAX_OSPATH];
	DIR *d;

	StripSlashes(PS3_AbsPath(path, abspath, sizeof(abspath)), clean, sizeof(clean));

	d = opendir(clean);

	if (d)
	{
		closedir(d);
		return true;
	}

	return false;
}

qboolean
Sys_IsFile(const char *path)
{
	char abspath[MAX_OSPATH];
	struct stat sb;

	path = PS3_AbsPath(path, abspath, sizeof(abspath));

	if (stat(path, &sb) != 0)
	{
		return false;
	}

	return !Sys_IsDir(path);
}

void
Sys_Mkdir(const char *path)
{
	char abspath[MAX_OSPATH], clean[MAX_OSPATH];

	if (Sys_IsDir(path))
	{
		return;
	}

	StripSlashes(PS3_AbsPath(path, abspath, sizeof(abspath)), clean, sizeof(clean));

	if (mkdir(clean, 0777) != 0 && !Sys_IsDir(clean))
	{
		Com_Error(ERR_FATAL, "Couldn't create dir %s\n", clean);
	}
}

char *
Sys_GetHomeDir(void)
{
	static char dir[MAX_OSPATH];

	if (!dir[0])
	{
		Com_sprintf(dir, sizeof(dir), "%s/", PS3_DATADIR);
	}

	Sys_Mkdir(dir);

	return dir;
}

void
Sys_Remove(const char *path)
{
	char abspath[MAX_OSPATH];

	path = PS3_AbsPath(path, abspath, sizeof(abspath));

	if (remove(path) == -1 && errno != ENOENT && Sys_IsFile(path))
	{
		Com_Printf("%s: remove %s failed\n", __func__, path);
	}
}

int
Sys_Rename(const char *from, const char *to)
{
	char absfrom[MAX_OSPATH], absto[MAX_OSPATH];

	return rename(PS3_AbsPath(from, absfrom, sizeof(absfrom)),
			PS3_AbsPath(to, absto, sizeof(absto)));
}

void
Sys_RemoveDir(const char *path)
{
	char abspath[MAX_OSPATH], clean[MAX_OSPATH];
	char filepath[MAX_OSPATH];
	struct dirent *file;
	DIR *directory;

	StripSlashes(PS3_AbsPath(path, abspath, sizeof(abspath)), clean, sizeof(clean));

	directory = opendir(clean);

	if (directory)
	{
		while ((file = readdir(directory)) != NULL)
		{
			if (!strcmp(file->d_name, ".") || !strcmp(file->d_name, ".."))
			{
				continue;
			}

			snprintf(filepath, MAX_OSPATH, "%s/%s", clean, file->d_name);
			Sys_Remove(filepath);
		}

		closedir(directory);
		rmdir(clean);
	}
}

qboolean
Sys_Realpath(const char *in, char *out, size_t size)
{
	char tmp[MAX_OSPATH];

	/* No realpath() here. There are no symlinks on /dev_hdd0 either, so
	   an absolute, normalized path that exists is as real as it gets. */
	MakeAbsolute(in, tmp, sizeof(tmp));

	if (!Sys_IsDir(tmp) && !Sys_IsFile(tmp))
	{
		Com_Printf("Couldn't get realpath for %s\n", in);
		return false;
	}

	Q_strlcpy(out, tmp, size);

	return true;
}

/* ================================================================ */

/* No shared libraries on the PS3. The renderer is linked in as well, the
   client asks for it by name (see src/client/vid/vid.c). */

void *
Sys_GetProcAddress(void *handle, const char *sym)
{
	return NULL;
}

void
Sys_FreeLibrary(void *handle)
{
}

void *
Sys_LoadLibrary(const char *path, const char *sym, void **handle)
{
	*handle = NULL;
	Com_Printf("%s: no shared libraries on the PS3 (%s)\n", __func__, path);

	return NULL;
}

/* ================================================================ */

void
Sys_GetWorkDir(char *buffer, size_t len)
{
	Q_strlcpy(buffer, workdir, len);
}

qboolean
Sys_SetWorkDir(char *path)
{
	char tmp[MAX_OSPATH];

	MakeAbsolute(path, tmp, sizeof(tmp));

	if (!Sys_IsDir(tmp))
	{
		return false;
	}

	Q_strlcpy(workdir, tmp, sizeof(workdir));

	return true;
}

/* ================================================================ */

/* Replaces src/backends/generic/misc.c */

const char *
Sys_GetBinaryDir(void)
{
	return PS3_USRDIR "/";
}

void
Sys_SetupFPU(void)
{
}
