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
 * PS3 display: RSX init, triple buffered flips, and the scaled blit of the
 * software renderer's image to the TV.
 *
 * =======================================================================
 */

#ifndef PS3_VIDEO_H
#define PS3_VIDEO_H

#include <stdint.h>

struct _gcmCtxData;   /* gcmContextData, <rsx/gcm_sys.h> */

int  PS3_Video_Init(void);            /* 0 ok, <0 error (see log); idempotent */
void PS3_Video_Shutdown(void);
int  PS3_Video_Ready(void);

/* Waits until the RSX has executed everything queued (before freeing
   memory it may use). */
void PS3_Video_Finish(void);

int  PS3_Video_Width(void);           /* TV mode, e.g. 1280x720 */
int  PS3_Video_Height(void);

/* The source image the renderer draws into (A8R8G8B8, rows of w pixels).
 * There is one per framebuffer; PS3_Video_Source() returns the one for the
 * frame being built, after making sure the RSX is done reading it. */
int       PS3_Video_SetSource(int w, int h);
uint32_t *PS3_Video_Source(void);
void      PS3_Video_FreeSource(void);

/* OpenGL mode (ref_gl1): the source images become the RSX's render
 * targets, w x h with rows padded to 64 bytes, plus a depth/stencil
 * buffer. BindRenderTarget waits for a free one and makes it the RSX's
 * surface; Present then scales it to the screen like the software
 * renderer's image. */
int  PS3_Video_SetRenderTarget(int w, int h);
void PS3_Video_BindRenderTarget(void);
struct _gcmCtxData *PS3_Video_Context(void);

/* Blits the current source to the screen (aspect kept, black borders)
 * and flips. */
void PS3_Video_Present(void);

/* Screen fit: percentage of the TV used by the picture (overscan), and
 * bilinear (1) or nearest (0) scaling. */
void PS3_Video_SetFit(int percent);
void PS3_Video_SetFilter(int linear);

/* Brightness (gain) and gamma of the final picture; 1, 1 = untouched.
   Anything else draws the picture through a fragment program. */
void PS3_Video_SetColor(float brightness, float gamma);

/* 1: at most 30 flips per second, evenly paced (one every 2 vblanks).
   0: flip on the next vblank (up to 60). */
void PS3_Video_SetLock30(int lock);

/* 1: fps and render timings in the log every 10 seconds (development,
   cvar vid_ps3_perflog). */
void PS3_Video_SetPerfLog(int on);

/* Profiling (log every 10 s): time the renderer spent writing the image
   into RSX memory this frame. */
void PS3_Video_ProfileCopy(long long usec);

/* Full screen text drawn by the CPU (fatal errors). Flips. */
void PS3_Video_TextScreen(const char *title, const char *const *lines, int nlines);

#endif
