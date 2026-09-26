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
 * ps3gl: init, frame bracket, frame fence, deferred frees, queries and
 * statistics. See ps3gl.h.
 *
 * =======================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "ps3gl.h"
#include "../ps3_platform.h"
#include "../ps3_video.h"

ps3gl_state_t ps3gl;

static int ready;
static int cull_flip;

/* ================================================================ */
/* Frame fence and deferred frees                                     */
/* ================================================================ */

#define MAX_PENDING_FREES 4096

typedef struct {
	void     *data;
	uint32_t  last_frame;
} pending_free_t;

static pending_free_t pending[MAX_PENDING_FREES];
static int npending;

uint32_t
ps3gl_completed_frame(void)
{
	if (!ps3gl.frame_label)
	{
		return 0;
	}

	return *ps3gl.frame_label;
}

/* Memory the RSX may still read (drawn with in 'last_frame') is freed
   once that frame is done. */
void
ps3gl_defer_free(void *data, uint32_t last_frame)
{
	if (!data)
	{
		return;
	}

	if (last_frame == 0 || npending >= MAX_PENDING_FREES)
	{
		if (last_frame != 0)
		{
			/* queue full: wait for the RSX rather than risk it */
			PS3_Video_Finish();
		}

		rsxFree(data);
		return;
	}

	pending[npending].data = data;
	pending[npending].last_frame = last_frame + PS3GL_FREE_DELAY;
	npending++;
}

void
ps3gl_process_frees(void)
{
	uint32_t done = ps3gl_completed_frame();
	int i = 0;

	while (i < npending)
	{
		if ((int)(done - pending[i].last_frame) >= 0 &&
			(int)(ps3gl.frame - pending[i].last_frame) >= 0)
		{
			rsxFree(pending[i].data);
			pending[i] = pending[--npending];
		}
		else
		{
			i++;
		}
	}
}

static void
WaitForFrame(uint32_t frame)
{
	int waited = 0;

	while ((int)(ps3gl_completed_frame() - frame) < 0)
	{
		usleep(20);

		if (++waited > 100000)
		{
			PS3_Log("[gl] WARNING: frame fence timed out (want %u, RSX at %u)",
					(unsigned)frame, (unsigned)ps3gl_completed_frame());
			break;
		}
	}
}

/* ================================================================ */

int
PS3GL_Ready(void)
{
	return ready;
}

int
PS3GL_Init(int width, int height)
{
	if (!ready)
	{
		ps3gl.ctx = PS3_Video_Context();

		if (!ps3gl.ctx)
		{
			PS3_Log("[gl] FATAL: no RSX context");
			return -1;
		}

		ps3gl.textures = (ps3gl_texture_t *)calloc(PS3GL_MAX_TEXTURES, sizeof(ps3gl_texture_t));

		if (!ps3gl.textures)
		{
			PS3_Log("[gl] FATAL: no memory for the texture table");
			return -1;
		}

		ps3gl.ring = (uint8_t *)rsxMemalign(128, PS3GL_RING_SEGMENTS * PS3GL_RING_SEG_SIZE);

		if (!ps3gl.ring)
		{
			PS3_Log("[gl] FATAL: rsxMemalign failed for the vertex ring (%d MB)",
					(PS3GL_RING_SEGMENTS * PS3GL_RING_SEG_SIZE) >> 20);
			return -1;
		}

		rsxAddressToOffset(ps3gl.ring, &ps3gl.ring_off);

		ps3gl.frame_label = (volatile uint32_t *)gcmGetLabelAddress(PS3GL_LABEL_FRAME);
		*ps3gl.frame_label = 0;
		ps3gl.frame = 0;

		ps3gl_textures_init();
		ps3gl_shaders_init();

		ready = 1;
		PS3_Log("[gl] ps3gl ready: ring %d x %d KB at offset 0x%08x",
				PS3GL_RING_SEGMENTS, PS3GL_RING_SEG_SIZE / 1024, (unsigned)ps3gl.ring_off);
	}

	ps3gl.screen_w = width;
	ps3gl.screen_h = height;

	ps3gl_states_reset();
	ps3gl_matrices_reset();

	ps3gl.color = 0xffffffffu;
	ps3gl.unpack_row_length = 0;
	memset(&ps3gl.va_vertex, 0, sizeof(ps3gl.va_vertex));
	memset(&ps3gl.va_color, 0, sizeof(ps3gl.va_color));
	memset(ps3gl.va_texcoord, 0, sizeof(ps3gl.va_texcoord));

	PS3_Log("[gl] render target %dx%d", width, height);

	return 0;
}

void
PS3GL_SetCullFlip(int flip)
{
	flip = flip ? 1 : 0;

	if (flip != cull_flip)
	{
		cull_flip = flip;
		ps3gl.dirty |= PS3GL_DIRTY_CULL;
		PS3_Log("[gl] cull flip %s", flip ? "ON" : "off");
	}
}

int
ps3gl_cull_flip(void)
{
	return cull_flip;
}

void
PS3GL_BeginFrame(void)
{
	int i;

	if (!ready)
	{
		return;
	}

	PS3_Video_BindRenderTarget();

	ps3gl.frame++;
	ps3gl.ring_seg = ps3gl.frame % PS3GL_RING_SEGMENTS;

	/* the segment was last used PS3GL_RING_SEGMENTS frames ago */
	if (ps3gl.frame > PS3GL_RING_SEGMENTS)
	{
		WaitForFrame(ps3gl.frame - PS3GL_RING_SEGMENTS);
	}

	if (ps3gl.ring_head > ps3gl.ring_peak)
	{
		ps3gl.ring_peak = ps3gl.ring_head;
	}

	ps3gl.ring_head = 0;

	/* Everything goes to the RSX again: the surface changed, and system
	   overlays may have touched the programs. */
	ps3gl.dirty = PS3GL_DIRTY_ALL;
	ps3gl.vp_loaded = 0;
	ps3gl.active_fp = -1;
	ps3gl.mvp_uploaded = 0;

	for (i = 0; i < PS3GL_MAX_TMUS; i++)
	{
		ps3gl.tmu[i].hw_tex = -2;
		ps3gl.tmu[i].hw_enabled = -1;
	}

	ps3gl_process_frees();
}

void
PS3GL_EndFrame(void)
{
	if (!ready)
	{
		return;
	}

	/* Written by the RSX once everything before it is drawn. */
	rsxSetWriteBackendLabel(ps3gl.ctx, PS3GL_LABEL_FRAME, ps3gl.frame);
}

void
PS3GL_LogStats(int frames)
{
	/* frames 0: just reset (the periodic log is off) */
	if (frames > 0)
	{
		PS3_Log("[gl]   %u draws, %u verts, %u KB uploaded per frame; ring peak %u KB; "
				"textures %u MB%s",
				(unsigned)(ps3gl.st_draws / frames), (unsigned)(ps3gl.st_verts / frames),
				(unsigned)(ps3gl.st_upload / frames / 1024), (unsigned)(ps3gl.ring_peak / 1024),
				(unsigned)(ps3gl.st_tex_bytes >> 20),
				ps3gl.st_dropped ? " -- DRAWS DROPPED (ring full)" : "");
	}

	ps3gl.st_draws = ps3gl.st_verts = ps3gl.st_upload = ps3gl.st_dropped = 0;
	ps3gl.ring_peak = 0;
}

/* ================================================================ */
/* Queries and things that do nothing here                            */
/* ================================================================ */

const GLubyte * APIENTRY
glGetString(GLenum name)
{
	switch (name)
	{
		case GL_VENDOR:     return (const GLubyte *)"Quake2PS3";
		case GL_RENDERER:   return (const GLubyte *)"RSX (ps3gl)";
		/* ref_gl1 wants 1.4; what it uses of 1.4 is covered */
		case GL_VERSION:    return (const GLubyte *)"1.4 ps3gl";
		case GL_EXTENSIONS: return (const GLubyte *)"GL_ARB_multitexture";
		default:            return (const GLubyte *)"";
	}
}

GLenum APIENTRY
glGetError(void)
{
	return GL_NO_ERROR;
}

void APIENTRY
glGetIntegerv(GLenum pname, GLint *params)
{
	if (!params)
	{
		return;
	}

	switch (pname)
	{
		case GL_MAX_TEXTURE_SIZE:  *params = 4096; break;
		case GL_MAX_TEXTURE_UNITS: *params = PS3GL_MAX_TMUS; break;
		case GL_DEPTH_BITS:        *params = 24; break;
		case GL_STENCIL_BITS:      *params = 8; break;
		default:                   *params = 0; break;
	}
}

void APIENTRY
glGetBooleanv(GLenum pname, GLboolean *params)
{
	if (!params)
	{
		return;
	}

	switch (pname)
	{
		case GL_DEPTH_WRITEMASK:
			params[0] = ps3gl.rs.depth_mask ? GL_TRUE : GL_FALSE;
			break;
		case GL_COLOR_WRITEMASK:
			params[0] = ps3gl.rs.mask_r;
			params[1] = ps3gl.rs.mask_g;
			params[2] = ps3gl.rs.mask_b;
			params[3] = ps3gl.rs.mask_a;
			break;
		default:
			params[0] = GL_FALSE;
			break;
	}
}

/* Waiting for the RSX here would only stall the frame (gl_finish). */
void APIENTRY glFinish(void) {}
void APIENTRY glFlush(void) {}
void APIENTRY glDrawBuffer(GLenum mode) { (void)mode; }
void APIENTRY glHint(GLenum target, GLenum mode) { (void)target; (void)mode; }
void APIENTRY glPointSize(GLfloat size) { (void)size; }
void APIENTRY glLineWidth(GLfloat width) { (void)width; }

/* Reading the RSX's memory from the CPU is very slow and screenshots
   aren't supported yet: black. */
void APIENTRY
glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format,
		GLenum type, GLvoid *pixels)
{
	int bpp = (format == GL_RGBA) ? 4 : 3;

	(void)x;
	(void)y;
	(void)type;

	if (pixels && width > 0 && height > 0)
	{
		memset(pixels, 0, (size_t)width * height * bpp);
	}
}
