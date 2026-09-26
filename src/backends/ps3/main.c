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
 * Entry point for the PS3. Boots in numbered steps, each one leaves a line
 * in quake2_log.txt, so a black screen can be located from the log alone:
 *
 *   [0] main() reached              -> if missing: -lrt -llv2 in the link
 *   [1] worker thread               -> the EBOOT's own stack is tiny
 *   [2] sysutil callback            -> without it, "Quit Game" reboots
 *   [3] file system (USRDIR, /dev_hdd0/data/quake2 writable)
 *   [4] game data + endianness      -> pak0.pak header, a BSP header
 *   [5] Qcommon_Init                -> the engine takes over (never returns)
 *
 * Stage 1 (PS3_STAGE1): no client, no video, no sound. The engine runs as
 * a headless single player server: loads a map, changes level (exercises
 * the save code and its relative paths), and quits by itself. The screen
 * stays black the whole time; the result is in the log.
 *
 * Stage 2: the full client with the software renderer (RSX blit) and the
 * DualShock 3, through the SDL shim (compat/SDL2/SDL.h, sdl_ps3.c).
 *
 * =======================================================================
 */

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef __PPU__
#include <ppu-types.h>
#include <sys/process.h>
#include <sys/thread.h>
#include <sysutil/sysutil.h>
#else
#include <pthread.h>
#endif

#include "../../common/header/common.h"
#include "../../server/header/server.h"
#include "ps3_platform.h"

#if defined(__PPU__) && !defined(DEDICATED_ONLY)
#include <unistd.h>
#include "ps3_video.h"
#include "ps3_pad.h"
#endif

#ifdef __PPU__
/* The loader sizes the primary thread's stack from this ELF section. */
SYS_PROCESS_PARAM(1001, 0x100000);
#endif

#define GAME_STACK_SIZE (4 * 1024 * 1024)

extern char datadir[MAX_OSPATH];   /* common/filesystem.c */

/* Map picked by the data probe: retail has base1, the demo has demo1. */
static char stage1_map1[16];
static char stage1_map2[16];

/* ================================================================ */

#ifdef __PPU__
static void
SysutilCallback(u64 status, u64 param, void *userdata)
{
	(void)param;
	(void)userdata;

	switch (status)
	{
		case SYSUTIL_EXIT_GAME:
			PS3_Log("[sysutil] SYSUTIL_EXIT_GAME -> quit on next frame");
			PS3_SetExitRequested();
			break;

		case SYSUTIL_DRAW_BEGIN:
		case SYSUTIL_DRAW_END:
			/* 0x121 / 0x131: XMB overlay opening/closing. */
			break;

		default:
			break;
	}
}
#endif

/* ================================================================ */
/* [3] file system                                                   */
/* ================================================================ */

static int
ProbeDir(const char *path, int list)
{
	DIR *d;
	struct dirent *e;
	int count = 0;

	d = opendir(path);

	if (!d)
	{
		PS3_Log("[3] %s -> can't open", path);
		return -1;
	}

	while ((e = readdir(d)) != NULL)
	{
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
		{
			continue;
		}

		if (list && count < 24)
		{
			PS3_Log("[3]     %s", e->d_name);
		}

		count++;
	}

	closedir(d);
	PS3_Log("[3] %s -> OK, %d entries", path, count);

	return count;
}

static void
ProbeFilesystem(void)
{
	FILE *f;

	PS3_Log("[3] file system");

	ProbeDir(PS3_USRDIR, 1);
	ProbeDir(PS3_USRDIR "/baseq2", 1);

	if (mkdir(PS3_DATADIR, 0777) == 0)
	{
		PS3_Log("[3] %s -> created", PS3_DATADIR);
	}

	f = fopen(PS3_DATADIR "/.writetest", "w");

	if (f)
	{
		fputs("ok", f);
		fclose(f);
		remove(PS3_DATADIR "/.writetest");
		PS3_Log("[3] %s -> WRITABLE", PS3_DATADIR);
		ProbeDir(PS3_DATADIR, 1);
	}
	else
	{
		PS3_Log("[3] %s -> NOT writable (config and saves will fail)", PS3_DATADIR);
	}
}

/* ================================================================ */
/* [4] game data + endianness                                        */
/* ================================================================ */

/* Little-endian reads, byte by byte: no casts, no swapping macros. The
   whole point is to have a reference that doesn't depend on the engine. */
static unsigned int
LE32(const unsigned char *p)
{
	return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
		((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

static int
ProbeData(void)
{
	char path[MAX_OSPATH];
	unsigned char hdr[12], entry[64], bsp[8];
	unsigned int dirofs, dirlen, nfiles, i;
	unsigned int bspofs = 0, bsplen = 0;
	unsigned int one = 1;
	int nmaps = 0, has_base1 = 0, has_demo1 = 0;
	FILE *f;

	PS3_Log("[4] CPU is %s-endian, sizeof(long)=%d sizeof(void*)=%d, char is %s",
			(*(unsigned char *)&one == 1) ? "little" : "big",
			(int)sizeof(long), (int)sizeof(void *),
			((char)0xff < 0) ? "signed" : "unsigned");

	Com_sprintf(path, sizeof(path), "%s/baseq2/pak0.pak", PS3_USRDIR);
	f = fopen(path, "rb");

	if (!f)
	{
		PS3_Log("[4] FATAL: %s not found. Upload baseq2/pak0.pak by FTP.", path);
		return -1;
	}

	if (fread(hdr, 1, 12, f) != 12 || memcmp(hdr, "PACK", 4) != 0)
	{
		PS3_Log("[4] FATAL: pak0.pak has no PACK header");
		fclose(f);
		return -1;
	}

	dirofs = LE32(hdr + 4);
	dirlen = LE32(hdr + 8);
	nfiles = dirlen / 64;

	PS3_Log("[4] pak0.pak: PACK, dirofs=%u dirlen=%u -> %u files", dirofs,
			dirlen, nfiles);

	if (fseek(f, (long)dirofs, SEEK_SET) != 0)
	{
		PS3_Log("[4] FATAL: can't seek to the pak directory");
		fclose(f);
		return -1;
	}

	for (i = 0; i < nfiles; i++)
	{
		if (fread(entry, 1, 64, f) != 64)
		{
			PS3_Log("[4] FATAL: pak directory truncated at entry %u", i);
			fclose(f);
			return -1;
		}

		entry[55] = '\0';

		if (!strncmp((char *)entry, "maps/", 5) && strstr((char *)entry, ".bsp"))
		{
			nmaps++;

			if (!strcmp((char *)entry, "maps/base1.bsp"))
			{
				has_base1 = 1;
				bspofs = LE32(entry + 56);
				bsplen = LE32(entry + 60);
			}
			else if (!strcmp((char *)entry, "maps/demo1.bsp") && !has_base1)
			{
				has_demo1 = 1;
				bspofs = LE32(entry + 56);
				bsplen = LE32(entry + 60);
			}
		}
	}

	PS3_Log("[4] %d maps in pak0.pak (%s)", nmaps,
			has_base1 ? "full game" : (has_demo1 ? "DEMO data" : "no start map!"));

	if (!has_base1 && !has_demo1)
	{
		fclose(f);
		return -1;
	}

	if (fseek(f, (long)bspofs, SEEK_SET) != 0 || fread(bsp, 1, 8, f) != 8)
	{
		PS3_Log("[4] FATAL: can't read the BSP header");
		fclose(f);
		return -1;
	}

	fclose(f);

	/* IBSP, version 38. Stored little-endian. */
	PS3_Log("[4] %s: ident=%c%c%c%c version=%u size=%u (expected IBSP 38)",
			has_base1 ? "base1.bsp" : "demo1.bsp",
			bsp[0], bsp[1], bsp[2], bsp[3], LE32(bsp + 4), bsplen);

	if (memcmp(bsp, "IBSP", 4) != 0 || LE32(bsp + 4) != 38)
	{
		return -1;
	}

	/* The engine's own swap functions must agree with the byte reads. */
	Swap_Init();
	PS3_Log("[4] engine: LittleLong(bytes 'IBSP' ver)=%d, bigendien=%d",
			LittleLong(*(int *)(bsp + 4)), (int)bigendien);

	if (LittleLong(*(int *)(bsp + 4)) != 38)
	{
		PS3_Log("[4] FATAL: engine byte swapping disagrees");
		return -1;
	}

	Q_strlcpy(stage1_map1, has_base1 ? "base1" : "demo1", sizeof(stage1_map1));
	Q_strlcpy(stage1_map2, has_base1 ? "base2" : "demo2", sizeof(stage1_map2));

	return 0;
}

/* ================================================================ */
/* Stage 1: scripted run, fed through Sys_ConsoleInput()             */
/* ================================================================ */

#ifdef PS3_STAGE1
char *
PS3_Stage1_ConsoleInput(void)
{
	static char cmd[64];
	static int step;
	static int start = -1;
	static int lastbeat;
	static int frames;
	int now = Sys_Milliseconds();

	frames++;

	if (start < 0)
	{
		start = now;
		lastbeat = now;
	}

	if (now - lastbeat >= 2000)
	{
		lastbeat = now;
		PS3_Log("[stage1] t=%ds loop=%d state=%d map=%s framenum=%d edicts=%d",
				(now - start) / 1000, frames, (int)sv.state, sv.name,
				sv.framenum, ge ? ge->num_edicts : -1);
	}

	switch (step)
	{
		case 0:
			if (now - start >= 6000 && sv.state == ss_game)
			{
				step++;
				PS3_Log("[stage1] level change -> %s (writes save/current)", stage1_map2);
				Com_sprintf(cmd, sizeof(cmd), "gamemap %s", stage1_map2);
				return cmd;
			}
			break;

		case 1:
			if (now - start >= 12000)
			{
				step++;
				PS3_Log("[stage1] listing save/current");
				return "dir save/current/*";
			}
			break;

		case 2:
			if (now - start >= 16000)
			{
				step++;
				PS3_Log("[stage1] done: quitting (back to the XMB)");
				return "quit";
			}
			break;

		default:
			break;
	}

	return NULL;
}
#endif

/* ================================================================ */

#if defined(__PPU__) && !defined(DEDICATED_ONLY)
/* Without data the engine would die with a cryptic error. Say what is
   missing and where it goes, then go back to the XMB (X, 60 seconds, or
   "Quit Game"). */
static void
NoDataScreen(void)
{
	static const char *const lines[] = {
		"The game data is missing or damaged.",
		"",
		"Copy the .pak files of YOUR copy of Quake II",
		"(GOG or Steam: the \"baseq2\" folder, not \"rerelease\")",
		"by FTP to:",
		"",
		"  " PS3_USRDIR "/baseq2/",
		"",
		"pak0.pak is required (about 175 MB).",
		"",
		"Press X to go back to the XMB.",
	};
	ps3_padstate_t st;
	unsigned prev = 0xffff;
	long long start;

	if (PS3_Video_Init() != 0)
	{
		return;
	}

	PS3_Video_TextScreen("QUAKE II", lines, sizeof(lines) / sizeof(lines[0]));
	PS3_Pad_Init();

	start = Sys_Microseconds();

	while (Sys_Microseconds() - start < 60000000ll && !PS3_ExitRequested())
	{
		PS3_Pump();
		PS3_Pad_Poll(&st);

		if ((st.buttons & PS3_PAD_CROSS) && !(prev & PS3_PAD_CROSS))
		{
			break;
		}

		prev = st.buttons;
		usleep(30000);
	}

	PS3_Pad_Shutdown();
	PS3_Video_Shutdown();
}
#endif

static void
GameThread(void)
{
	static char *argv[16];
	int argc = 0;

	PS3_Log("[1] game thread running");

#ifdef __PPU__
	PS3_Log("[2] registering the sysutil callback");
	sysUtilRegisterCallback(0, SysutilCallback, NULL);
#else
	PS3_Log("[2] (host build, no sysutil)");
#endif

	ProbeFilesystem();

	PS3_Log("[4] game data");

	if (ProbeData() != 0)
	{
		PS3_Log("[4] FATAL: no usable game data, stopping here");
#if defined(__PPU__) && !defined(DEDICATED_ONLY)
		NoDataScreen();
#endif
		return;
	}

	Q_strlcpy(datadir, PS3_USRDIR, MAX_OSPATH);

	argv[argc++] = "quake2";
	/* The PS3 never passes real arguments. Extra ones (for testing) can
	   go in autoexec.cfg. */
#ifdef PS3_STAGE1
	argv[argc++] = "+set";
	argv[argc++] = "singleplayer";
	argv[argc++] = "1";
	argv[argc++] = "+set";
	argv[argc++] = "developer";
	argv[argc++] = "1";
	argv[argc++] = "+map";
	argv[argc++] = stage1_map1;
#endif
	argv[argc] = NULL;

	PS3_Log("[5] Qcommon_Init (datadir %s, home %s)", datadir, PS3_DATADIR);

	/* Never returns: leaves through Sys_Quit() / Sys_Error(). */
	Qcommon_Init(argc, argv);
}

#ifdef __PPU__

static void
GameThreadEntry(void *arg)
{
	(void)arg;
	GameThread();
	PS3_LogShutdown();
	sysThreadExit(0);
}

int
main(int argc, char **argv)
{
	sys_ppu_thread_t tid;
	u64 exit_code;

	(void)argc;
	(void)argv;

	PS3_LogInit();
	PS3_Log("[0] main() reached (link OK: -lrt -llv2)");

	if (sysThreadCreate(&tid, GameThreadEntry, NULL, 1000, GAME_STACK_SIZE,
				THREAD_JOINABLE, "quake2") != 0)
	{
		PS3_Log("[0] FATAL: sysThreadCreate failed");
		PS3_LogShutdown();
		return 1;
	}

	sysThreadJoin(tid, &exit_code);

	return 0;
}

#else /* host harness */

static void *
GameThreadEntry(void *arg)
{
	(void)arg;
	GameThread();
	PS3_LogShutdown();
	return NULL;
}

int
main(int argc, char **argv)
{
	pthread_t tid;
	pthread_attr_t attr;

	(void)argc;
	(void)argv;

	PS3_LogInit();
	PS3_Log("[0] main() reached (host harness)");

	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, GAME_STACK_SIZE);

	if (pthread_create(&tid, &attr, GameThreadEntry, NULL) != 0)
	{
		PS3_Log("[0] FATAL: pthread_create failed");
		return 1;
	}

	pthread_join(tid, NULL);

	return 0;
}

#endif
