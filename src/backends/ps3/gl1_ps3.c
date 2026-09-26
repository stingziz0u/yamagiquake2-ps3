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
 * ref_gl1's platform part on the PS3, replacing gl1_sdl.c. Built into
 * the ref_gl1 object (see ps3/Makefile).
 *
 * There is no GL context to create: ps3gl (src/backends/ps3/gl/) draws
 * into a render target of the size of the chosen mode ("internal res"),
 * and every frame that image is scaled to the TV like the software
 * renderer's (PS3_Video_Present).
 *
 * Gamma is applied to the textures when they are loaded (GL1_GAMMATABLE,
 * set in ps3/Makefile), there's no hardware gamma ramp: the video menu
 * restarts the renderer when the brightness changes.
 *
 * =======================================================================
 */

#include "../../client/refresh/gl1/header/local.h"

#include "ps3_video.h"
#include "gl/ps3gl.h"

/* glimp_ps3.c: the TV screen fit / filter / frame rate cvars */
void GLimp_PS3_ApplyVideoCvars(void);

qboolean IsHighDPIaware = false;

static cvar_t *gl_ps3_cullflip;

void
RI_EndFrame(void)
{
	R_ApplyGLBuffer();	/* buffered 2D (console, HUD text) */

	if (gl_ps3_cullflip && gl_ps3_cullflip->modified)
	{
		gl_ps3_cullflip->modified = false;
		PS3GL_SetCullFlip((int)gl_ps3_cullflip->value);
	}

	GLimp_PS3_ApplyVideoCvars();

	PS3GL_EndFrame();
	PS3_Video_Present();

	/* the next frame's GL calls may come any time now (loading...) */
	PS3GL_BeginFrame();
}

void *
RI_GetProcAddress(const char *proc)
{
	/* the only extensions ref_gl1 looks up that ps3gl has */
	if (!strcmp(proc, "glActiveTexture") || !strcmp(proc, "glActiveTextureARB"))
	{
		return (void *)glActiveTexture;
	}

	if (!strcmp(proc, "glClientActiveTexture") || !strcmp(proc, "glClientActiveTextureARB"))
	{
		return (void *)glClientActiveTexture;
	}

	return NULL;
}

qboolean
RI_IsVSyncActive(void)
{
	/* flips happen on vblank */
	return true;
}

int
RI_PrepareForWindow(void)
{
	/* the depth buffer is Z24S8 */
	gl_state.stencil = true;

	return 0;
}

void
RI_SetVsync(void)
{
}

void
RI_UpdateGamma(void)
{
	/* GL1_GAMMATABLE: gamma is in the textures */
}

int
RI_InitContext(void *win)
{
	const char *glver;

	if (win == NULL)
	{
		Com_Error(ERR_FATAL, "%s must not be called with NULL argument!", __func__);
		return false;
	}

	if (PS3_Video_SetRenderTarget(vid.width, vid.height) != 0)
	{
		Com_Printf("%s: can't allocate a %dx%d render target\n", __func__,
				vid.width, vid.height);
		return false;
	}

	if (PS3GL_Init(vid.width, vid.height) != 0)
	{
		Com_Printf("%s: ps3gl failed to start (see the log)\n", __func__);
		return false;
	}

	/* One copy of each lightmap per frame in flight: dynamic lights
	   rewrite a copy the RSX is done with, never the one it's drawing
	   with (RI_Init reads this right after). */
	ri.Cvar_Set("gl1_tilerendering", "1");

	gl_ps3_cullflip = ri.Cvar_Get("gl_ps3_cullflip", "0", 0);
	PS3GL_SetCullFlip((int)gl_ps3_cullflip->value);
	gl_ps3_cullflip->modified = false;

	PS3GL_BeginFrame();

	glver = (const char *)glGetString(GL_VERSION);
	sscanf(glver, "%d.%d", &gl_config.major_version, &gl_config.minor_version);

	vid_gamma->modified = true;

	Com_Printf("PS3 OpenGL (ps3gl on the RSX): %dx%d, scaled to the TV\n",
			vid.width, vid.height);

	return true;
}

void
RI_GetDrawableSize(int *width, int *height)
{
	*width = vid.width;
	*height = vid.height;
}

void
RI_ShutdownContext(void)
{
	/* The render target stays until the next mode or renderer takes the
	   video memory over (PS3_Video_SetRenderTarget / SetSource). */
}

int
RI_GetSDLVersion(void)
{
	/* must match GLimp_GetFrameworkVersion() in glimp_ps3.c */
	return 2;
}
