/*
 * Copyright (C) 2010 Yamagi Burmeister
 * Copyright (C) 2026 the Quake2PS3 contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * =======================================================================
 *
 * Client side video glue for the PS3, replacing
 * src/client/vid/glimp_sdl2.c. There is one "window": the TV. The RSX is
 * brought up once (GLimp_Init) and stays up until the game exits; mode
 * changes only change the size of the image the renderer draws (the
 * software renderer's image, or ref_gl1's render target), which the RSX
 * scales to the screen.
 *
 * =======================================================================
 */

#include "../../common/header/common.h"
#include "../../client/vid/header/ref.h"

#include "ps3_platform.h"
#include "ps3_video.h"

float glimp_refreshRate = -1.0f;

/* Read by sdl_ps3.c before each present. */
cvar_t *vid_ps3_screenfit;
cvar_t *vid_ps3_filter;
cvar_t *vid_ps3_fps;
cvar_t *vid_ps3_brightness;
cvar_t *vid_ps3_gamma;
cvar_t *vid_ps3_perflog;

/* Whatever non-NULL pointer: the renderer only checks it isn't NULL. */
static int the_window;
static qboolean initSuccessful;
static char *displayindices[] = {"0: TV", NULL};

/* Applies the TV settings before each present (both renderers). */
void
GLimp_PS3_ApplyVideoCvars(void)
{
	if (vid_ps3_screenfit && vid_ps3_screenfit->modified)
	{
		vid_ps3_screenfit->modified = false;
		PS3_Video_SetFit((int)vid_ps3_screenfit->value);
	}

	if (vid_ps3_filter && vid_ps3_filter->modified)
	{
		vid_ps3_filter->modified = false;
		PS3_Video_SetFilter((int)vid_ps3_filter->value);
	}

	if (vid_ps3_fps && vid_ps3_fps->modified)
	{
		vid_ps3_fps->modified = false;
		PS3_Video_SetLock30((int)vid_ps3_fps->value == 30);
	}

	if ((vid_ps3_brightness && vid_ps3_brightness->modified) ||
		(vid_ps3_gamma && vid_ps3_gamma->modified))
	{
		vid_ps3_brightness->modified = false;
		vid_ps3_gamma->modified = false;
		PS3_Video_SetColor(vid_ps3_brightness->value, vid_ps3_gamma->value);
	}

	if (vid_ps3_perflog && vid_ps3_perflog->modified)
	{
		vid_ps3_perflog->modified = false;
		PS3_Video_SetPerfLog((int)vid_ps3_perflog->value);
	}
}

/* Settings that change once when a new version brings a better default,
   over what config.cfg kept from before. */
static void
MigrateConfig(void)
{
	cvar_t *ver = Cvar_Get("vid_ps3_cfgver", "0", CVAR_ARCHIVE);

	if (ver->value < 3)
	{
		/* 0.3: the OpenGL renderer on the RSX becomes the default, at
		   the TV's own 1280x720. Lightmap copies per frame in flight. */
		Com_Printf("PS3: switching to the OpenGL renderer, 1280x720\n");
		Cvar_Set("vid_renderer", "gl1");
		Cvar_Set("r_mode", "-1");
		Cvar_Set("r_customwidth", "1280");
		Cvar_Set("r_customheight", "720");
		Cvar_Set("gl1_tilerendering", "1");
		Cvar_Set("vid_ps3_cfgver", "3");
	}

	if (ver->value < 4)
	{
		/* 0.4: brightness and gamma move to the final pass (the old
		   brightness slider was vid_gamma), look sensitivity becomes a
		   0.5..8 scale (back to the default). */
		Com_Printf("PS3: new brightness/gamma, look sensitivity and rumble defaults\n");
		Cvar_Set("vid_ps3_brightness", "1");
		Cvar_Set("vid_ps3_gamma", "1");
		Cvar_Set("joy_sensitivity", "3");
		Cvar_Set("joy_yawspeed", "160");
		Cvar_Set("joy_pitchspeed", "120");
		Cvar_Set("joy_extra_yawspeed", "220");
		Cvar_Set("joy_extra_pitchspeed", "0");
		Cvar_Set("joy_ramp_time", "0.35");
		Cvar_Set("joy_haptic_magnitude", "1");
		Cvar_Set("vid_ps3_cfgver", "4");
	}

	if (ver->value < 5)
	{
		/* 0.5: no rumble on the PS3 build */
		Cvar_Set("joy_haptic_magnitude", "0");
		Cvar_Set("vid_ps3_cfgver", "5");
	}

	if (ver->value < 6)
	{
		/* 0.6: the development timings are off (r_dspeeds was set by the
		   test builds' yq2.cfg and kept in config.cfg) */
		Cvar_Set("r_dspeeds", "0");
		Cvar_Set("vid_ps3_cfgver", "6");
	}
}

qboolean
GLimp_Init(void)
{
	/* Same cvars as glimp_sdl2.c, the video menu reads them. */
	Cvar_Get("vid_displayrefreshrate", "-1", CVAR_ARCHIVE);
	Cvar_Get("vid_displayindex", "0", CVAR_ARCHIVE);
	Cvar_Get("vid_highdpiaware", "0", CVAR_ARCHIVE);
	Cvar_Get("vid_rate", "-1", CVAR_ARCHIVE);

	/* Percentage of the TV used by the picture (overscan) and scaling
	   filter (1 = bilinear, 0 = nearest). */
	vid_ps3_screenfit = Cvar_Get("vid_ps3_screenfit", "90", CVAR_ARCHIVE);
	vid_ps3_filter = Cvar_Get("vid_ps3_filter", "1", CVAR_ARCHIVE);
	/* 0 = up to 60, 30 = locked 30 (even pace) */
	vid_ps3_fps = Cvar_Get("vid_ps3_fps", "0", CVAR_ARCHIVE);
	/* Brightness (gain) and gamma, applied to the final picture by the
	   RSX (ps3_video.c). The renderers' own gamma stays neutral: ref_gl1
	   would bake it into the textures, the software renderer into its
	   palette. */
	vid_ps3_brightness = Cvar_Get("vid_ps3_brightness", "1", CVAR_ARCHIVE);
	vid_ps3_gamma = Cvar_Get("vid_ps3_gamma", "1", CVAR_ARCHIVE);

	/* Development: fps and render timings in the log every 10 s. Off, so
	   the log doesn't grow with play time. */
	vid_ps3_perflog = Cvar_Get("vid_ps3_perflog", "0", CVAR_ARCHIVE);
	vid_ps3_perflog->modified = true;

	vid_ps3_screenfit->modified = true;
	vid_ps3_filter->modified = true;
	vid_ps3_fps->modified = true;

	MigrateConfig();

	vid_ps3_brightness->modified = true;
	Cvar_Set("vid_gamma", "1");

	Com_Printf("-------- vid initialization --------\n");

	if (PS3_Video_Init() != 0)
	{
		Com_Printf("Couldn't initialize the RSX.\n");
		return false;
	}

	Com_Printf("PS3 display: %dx%d\n", PS3_Video_Width(), PS3_Video_Height());
	Com_Printf("------------------------------------\n\n");

	return true;
}

void
GLimp_Shutdown(void)
{
	initSuccessful = false;
	/* The RSX itself is shut down by Sys_Quit(), after everything else. */
}

qboolean
GLimp_InitGraphics(int fullscreen, int *pwidth, int *pheight)
{
	(void)fullscreen;

	/* A mode change: let the renderer drop its buffers first. */
	if (initSuccessful)
	{
		re.ShutdownContext();
		initSuccessful = false;
	}

	if (re.PrepareForWindow() == -1)
	{
		return false;
	}

	/* The transfer unit that scales the image to the TV takes at most
	   2047 pixels per side. */
	if (*pwidth > 2047 || *pheight > 2047)
	{
		Com_Printf("%dx%d is too big for the PS3, using 1280x720.\n", *pwidth, *pheight);
		*pwidth = 1280;
		*pheight = 720;
		Cvar_SetValue("r_mode", 14);
	}

	/* the renderer reads this through vid, before InitContext() */
	viddef.width = *pwidth;
	viddef.height = *pheight;

	if (!re.InitContext(&the_window))
	{
		return false;
	}

	Com_Printf("Drawable size: %ix%i\n", viddef.width, viddef.height);
	initSuccessful = true;

	return true;
}

void
GLimp_ShutdownGraphics(void)
{
	initSuccessful = false;
}

void
GLimp_GrabInput(qboolean grab)
{
	(void)grab;
}

float
GLimp_GetRefreshRate(void)
{
	/* flips are vsynced; 50 Hz TVs are not handled specially yet */
	return 60.0f;
}

qboolean
GLimp_GetDesktopMode(int *pwidth, int *pheight)
{
	*pwidth = PS3_Video_Width();
	*pheight = PS3_Video_Height();

	return true;
}

const char **
GLimp_GetDisplayIndices(void)
{
	return (const char **)displayindices;
}

int
GLimp_GetNumVideoDisplays(void)
{
	return 1;
}

int
GLimp_GetWindowDisplayIndex(void)
{
	return 0;
}

int
GLimp_GetFrameworkVersion(void)
{
	/* must match what the renderer reports (SDL major version) */
	return 2;
}
