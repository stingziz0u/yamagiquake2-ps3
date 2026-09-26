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
 * ps3gl: client arrays, drawing and the shaders.
 *
 * Every draw copies the vertices it uses into this frame's segment of the
 * vertex ring (36 byte interleaved vertices, IoQuake3-PS3's format) and
 * draws from there: ref_gl1's arrays are in ordinary memory, which the
 * RSX can't read (only the IO mapped window). Indices go inline in the
 * command buffer (rsxDrawInlineIndexArray16), like IoQuake3-PS3.
 *
 * The fragment program follows from the texture units:
 *   no texture                         -> color
 *   unit 0, GL_REPLACE                 -> texture
 *   unit 0, GL_MODULATE / GL_COMBINE   -> texture * color
 *   units 0 + 1 (world + lightmap)     -> texture * lightmap (x2, x4 with
 *                                         GL_COMBINE + GL_RGB_SCALE)
 *
 * =======================================================================
 */

#include <stdio.h>

#include "ps3gl.h"
#include "ps3gl_shader_data.h"
#include "../ps3_platform.h"

static rsxVertexProgram *vp;
static void *vp_ucode;
static u32 vp_ucode_size;
static rsxProgramConst *vp_mvp;

static struct {
	rsxFragmentProgram *fp;
	void *ucode;           /* copy in RSX memory */
	u32 size;
	u32 offset;
} fps[PS3GL_FP_COUNT];

/* ================================================================ */
/* Shaders                                                            */
/* ================================================================ */

static void
LoadFP(int slot, const unsigned char *data, const char *name)
{
	void *ucode;

	fps[slot].fp = (rsxFragmentProgram *)data;
	rsxFragmentProgramGetUCode(fps[slot].fp, &ucode, &fps[slot].size);

	/* the fragment program's code must be in RSX memory */
	fps[slot].ucode = rsxMemalign(64, fps[slot].size);

	if (!fps[slot].ucode)
	{
		PS3_Log("[gl] FATAL: rsxMemalign failed for fragment program %s", name);
		return;
	}

	memcpy(fps[slot].ucode, ucode, fps[slot].size);
	rsxAddressToOffset(fps[slot].ucode, &fps[slot].offset);
}

void
ps3gl_shaders_init(void)
{
	vp = (rsxVertexProgram *)q2_vp_vpo;
	rsxVertexProgramGetUCode(vp, &vp_ucode, &vp_ucode_size);
	vp_mvp = rsxVertexProgramGetConst(vp, "mvp");

	LoadFP(PS3GL_FP_COLOR, q2_fp_color_fpo, "color");
	LoadFP(PS3GL_FP_MODULATE, q2_fp_modulate_fpo, "modulate");
	LoadFP(PS3GL_FP_REPLACE, q2_fp_replace_fpo, "replace");
	LoadFP(PS3GL_FP_LIGHTMAP, q2_fp_lightmap_fpo, "lightmap");
	LoadFP(PS3GL_FP_LIGHTMAP2X, q2_fp_lightmap2x_fpo, "lightmap2x");
	LoadFP(PS3GL_FP_LIGHTMAP4X, q2_fp_lightmap4x_fpo, "lightmap4x");

	PS3_Log("[gl] shaders: vertex program %u bytes (mvp %s), %d fragment programs",
			(unsigned)vp_ucode_size, vp_mvp ? "ok" : "MISSING", PS3GL_FP_COUNT);
}

void
ps3gl_apply_shader(int fp)
{
	gcmContextData *ctx = ps3gl.ctx;

	if (!ps3gl.vp_loaded)
	{
		rsxLoadVertexProgram(ctx, vp, vp_ucode);
		ps3gl.vp_loaded = 1;
		ps3gl.mvp_uploaded = 0;
	}

	if (fp != ps3gl.active_fp && fps[fp].ucode)
	{
		rsxLoadFragmentProgramLocation(ctx, fps[fp].fp, fps[fp].offset, GCM_LOCATION_RSX);
		ps3gl.active_fp = fp;
	}

	if (!ps3gl.mvp_uploaded && vp_mvp)
	{
		rsxSetVertexProgramParameter(ctx, vp, vp_mvp, ps3gl.mvp);
		ps3gl.mvp_uploaded = 1;
	}
}

static int
UnitHasTexture(int i)
{
	const ps3gl_tmu_t *tmu = &ps3gl.tmu[i];

	return tmu->enabled && ps3gl.textures[tmu->tex].data &&
		ps3gl.textures[tmu->tex].num_levels > 0;
}

/* Fragment program for the current texture units; *units = how many
   samplers it uses. */
static int
ChooseFP(int *units)
{
	int t0 = UnitHasTexture(0);
	int t1 = UnitHasTexture(1);

	if (t0 && t1)
	{
		const ps3gl_tmu_t *lm = &ps3gl.tmu[1];
		int scale = (lm->env == GL_COMBINE) ? lm->rgb_scale : 1;

		*units = 2;

		if (scale >= 4)
		{
			return PS3GL_FP_LIGHTMAP4X;
		}

		if (scale >= 2)
		{
			return PS3GL_FP_LIGHTMAP2X;
		}

		return PS3GL_FP_LIGHTMAP;
	}

	if (t0)
	{
		*units = 1;

		return (ps3gl.tmu[0].env == GL_REPLACE) ? PS3GL_FP_REPLACE : PS3GL_FP_MODULATE;
	}

	*units = 0;

	return PS3GL_FP_COLOR;
}

/* ================================================================ */
/* Client arrays                                                      */
/* ================================================================ */

static ps3gl_array_t *
ArrayFor(GLenum array)
{
	switch (array)
	{
		case GL_VERTEX_ARRAY:        return &ps3gl.va_vertex;
		case GL_COLOR_ARRAY:         return &ps3gl.va_color;
		case GL_TEXTURE_COORD_ARRAY: return &ps3gl.va_texcoord[ps3gl.client_tmu];
		default:                     return NULL;
	}
}

void APIENTRY
glEnableClientState(GLenum array)
{
	ps3gl_array_t *a = ArrayFor(array);

	if (a)
	{
		a->enabled = 1;
	}
}

void APIENTRY
glDisableClientState(GLenum array)
{
	ps3gl_array_t *a = ArrayFor(array);

	if (a)
	{
		a->enabled = 0;
	}
}

static void
SetArray(ps3gl_array_t *a, GLint size, GLenum type, GLsizei stride, const GLvoid *ptr)
{
	a->size = size;
	a->type = type;
	a->stride = stride;
	a->ptr = ptr;
}

void APIENTRY
glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr)
{
	SetArray(&ps3gl.va_vertex, size, type, stride, ptr);
}

void APIENTRY
glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr)
{
	SetArray(&ps3gl.va_texcoord[ps3gl.client_tmu], size, type, stride, ptr);
}

void APIENTRY
glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr)
{
	SetArray(&ps3gl.va_color, size, type, stride, ptr);
}

static int
TypeSize(GLenum type)
{
	switch (type)
	{
		case GL_BYTE:
		case GL_UNSIGNED_BYTE:  return 1;
		case GL_SHORT:
		case GL_UNSIGNED_SHORT: return 2;
		case GL_DOUBLE:         return 8;
		default:                return 4;
	}
}

static int
Stride(const ps3gl_array_t *a)
{
	return a->stride ? a->stride : a->size * TypeSize(a->type);
}

/* Component c of element i as a float (0 if missing) */
static inline float
Fetch(const ps3gl_array_t *a, const uint8_t *e, int c)
{
	if (c >= a->size)
	{
		return (c == 3) ? 1.0f : 0.0f;
	}

	switch (a->type)
	{
		case GL_FLOAT:  return ((const float *)e)[c];
		case GL_SHORT:  return (float)((const int16_t *)e)[c];
		case GL_INT:    return (float)((const int32_t *)e)[c];
		case GL_DOUBLE: return (float)((const double *)e)[c];
		default:        return 0.0f;
	}
}

static inline uint32_t
FetchColor(const ps3gl_array_t *a, const uint8_t *e)
{
	if (a->type == GL_UNSIGNED_BYTE)
	{
		/* bytes R G B A, the vertex format's order */
		if (a->size == 4)
		{
			return ((uint32_t)e[0] << 24) | ((uint32_t)e[1] << 16) |
				((uint32_t)e[2] << 8) | e[3];
		}

		return ((uint32_t)e[0] << 24) | ((uint32_t)e[1] << 16) | ((uint32_t)e[2] << 8) | 0xff;
	}

	if (a->type == GL_FLOAT)
	{
		const float *f = (const float *)e;
		uint32_t c = 0;
		int i;

		for (i = 0; i < 4; i++)
		{
			float v = (i < a->size) ? f[i] : 1.0f;
			uint32_t b = v <= 0.0f ? 0 : v >= 1.0f ? 255 : (uint32_t)(v * 255.0f + 0.5f);

			c |= b << (24 - 8 * i);
		}

		return c;
	}

	return 0xffffffffu;
}

/* Copies vertices first..first+count-1 into the ring and gives the RSX
   offset of the copy. 0 if the ring segment is full. Texture coordinates
   are only read for the units the fragment program samples: an array
   left enabled on an unused unit may point at anything. */
static int
CopyVertices(int first, int count, int units, uint32_t *ring_offset)
{
	const ps3gl_array_t *va = &ps3gl.va_vertex;
	const ps3gl_array_t *vc = ps3gl.va_color.enabled ? &ps3gl.va_color : NULL;
	const ps3gl_array_t *vt0 = (units >= 1 && ps3gl.va_texcoord[0].enabled) ?
		&ps3gl.va_texcoord[0] : NULL;
	const ps3gl_array_t *vt1 = (units >= 2 && ps3gl.va_texcoord[1].enabled) ?
		&ps3gl.va_texcoord[1] : NULL;
	uint32_t bytes = (uint32_t)count * PS3GL_VERTEX_SIZE;
	uint32_t seg_base = ps3gl.ring_seg * PS3GL_RING_SEG_SIZE;
	const uint8_t *pv, *pc = NULL, *pt0 = NULL, *pt1 = NULL;
	int sv, sc = 0, st0 = 0, st1 = 0;
	ps3gl_vertex_t *out;
	uint32_t color = ps3gl.color;
	int fast, i;

	if (ps3gl.ring_head + bytes > PS3GL_RING_SEG_SIZE)
	{
		static int warned;

		if (!warned)
		{
			PS3_Log("[gl] WARNING: vertex ring segment full (%u + %u > %u), draws dropped",
					(unsigned)ps3gl.ring_head, (unsigned)bytes, PS3GL_RING_SEG_SIZE);
			warned = 1;
		}

		ps3gl.st_dropped++;
		return 0;
	}

	out = (ps3gl_vertex_t *)(ps3gl.ring + seg_base + ps3gl.ring_head);

	sv = Stride(va);
	pv = (const uint8_t *)va->ptr + (size_t)first * sv;

	if (vc && vc->ptr)
	{
		sc = Stride(vc);
		pc = (const uint8_t *)vc->ptr + (size_t)first * sc;
	}

	if (vt0 && vt0->ptr)
	{
		st0 = Stride(vt0);
		pt0 = (const uint8_t *)vt0->ptr + (size_t)first * st0;
	}

	if (vt1 && vt1->ptr)
	{
		st1 = Stride(vt1);
		pt1 = (const uint8_t *)vt1->ptr + (size_t)first * st1;
	}

	/* ref_gl1's usual case: float positions, float texcoords, byte colors */
	fast = (va->type == GL_FLOAT) &&
		(!pt0 || (vt0->type == GL_FLOAT && vt0->size >= 2)) &&
		(!pt1 || (vt1->type == GL_FLOAT && vt1->size >= 2)) &&
		(!pc || (vc->type == GL_UNSIGNED_BYTE && vc->size == 4));

	for (i = 0; i < count; i++, out++)
	{
		if (fast)
		{
			const float *p = (const float *)pv;

			out->x = p[0];
			out->y = p[1];
			out->z = (va->size > 2) ? p[2] : 0.0f;
			out->w = 1.0f;

			if (pt0)
			{
				out->u0 = ((const float *)pt0)[0];
				out->v0 = ((const float *)pt0)[1];
				pt0 += st0;
			}
			else
			{
				out->u0 = out->v0 = 0.0f;
			}

			if (pt1)
			{
				out->u1 = ((const float *)pt1)[0];
				out->v1 = ((const float *)pt1)[1];
				pt1 += st1;
			}
			else
			{
				out->u1 = out->v1 = 0.0f;
			}

			if (pc)
			{
				out->color = ((uint32_t)pc[0] << 24) | ((uint32_t)pc[1] << 16) |
					((uint32_t)pc[2] << 8) | pc[3];
				pc += sc;
			}
			else
			{
				out->color = color;
			}
		}
		else
		{
			out->x = Fetch(va, pv, 0);
			out->y = Fetch(va, pv, 1);
			out->z = Fetch(va, pv, 2);
			out->w = 1.0f;

			if (pt0)
			{
				out->u0 = Fetch(vt0, pt0, 0);
				out->v0 = Fetch(vt0, pt0, 1);
				pt0 += st0;
			}
			else
			{
				out->u0 = out->v0 = 0.0f;
			}

			if (pt1)
			{
				out->u1 = Fetch(vt1, pt1, 0);
				out->v1 = Fetch(vt1, pt1, 1);
				pt1 += st1;
			}
			else
			{
				out->u1 = out->v1 = 0.0f;
			}

			if (pc)
			{
				out->color = FetchColor(vc, pc);
				pc += sc;
			}
			else
			{
				out->color = color;
			}
		}

		pv += sv;
	}

	*ring_offset = ps3gl.ring_off + seg_base + ps3gl.ring_head;

	/* Every block starts 16 byte aligned: otherwise the attribute offsets
	   drift and the RSX's vertex fetch hangs the console (IoQuake3-PS3's
	   note). */
	ps3gl.ring_head = (ps3gl.ring_head + bytes + 15u) & ~15u;

	return 1;
}

static void
BindRing(uint32_t off)
{
	gcmContextData *ctx = ps3gl.ctx;

	rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_POS, 0, off + PS3GL_VATTR_POS_OFF,
			PS3GL_VERTEX_SIZE, 4, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
	rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_TEX0, 0, off + PS3GL_VATTR_TC0_OFF,
			PS3GL_VERTEX_SIZE, 2, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
	rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_TEX1, 0, off + PS3GL_VATTR_TC1_OFF,
			PS3GL_VERTEX_SIZE, 2, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
	rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_COLOR0, 0, off + PS3GL_VATTR_COLOR_OFF,
			PS3GL_VERTEX_SIZE, 4, GCM_VERTEX_DATA_TYPE_U8, GCM_LOCATION_RSX);
}

static int
Prim(GLenum mode, uint32_t *prim)
{
	switch (mode)
	{
		case GL_POINTS:         *prim = GCM_TYPE_POINTS; return 1;
		case GL_LINES:          *prim = GCM_TYPE_LINES; return 1;
		case GL_LINE_LOOP:      *prim = GCM_TYPE_LINE_LOOP; return 1;
		case GL_LINE_STRIP:     *prim = GCM_TYPE_LINE_STRIP; return 1;
		case GL_TRIANGLES:      *prim = GCM_TYPE_TRIANGLES; return 1;
		case GL_TRIANGLE_STRIP: *prim = GCM_TYPE_TRIANGLE_STRIP; return 1;
		case GL_TRIANGLE_FAN:   *prim = GCM_TYPE_TRIANGLE_FAN; return 1;
		case GL_QUADS:          *prim = GCM_TYPE_QUADS; return 1;
		case GL_QUAD_STRIP:     *prim = GCM_TYPE_QUAD_STRIP; return 1;
		case GL_POLYGON:        *prim = GCM_TYPE_POLYGON; return 1;
		default:                return 0;
	}
}

/* State, matrices, textures and shader to the RSX. Gives the number of
   texture units in use. */
static int
PrepareDraw(int *units_out)
{
	int units, fp;

	if (!ps3gl.ctx || !ps3gl.ring || !ps3gl.va_vertex.enabled || !ps3gl.va_vertex.ptr)
	{
		return 0;
	}

	fp = ChooseFP(&units);

	ps3gl_apply_states();
	ps3gl_apply_matrices();
	ps3gl_apply_textures(units);
	ps3gl_apply_shader(fp);

	*units_out = units;

	return 1;
}

void APIENTRY
glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
	uint32_t prim, off;
	int units;

	if (count <= 0 || first < 0 || !Prim(mode, &prim) || !PrepareDraw(&units))
	{
		return;
	}

	if (!CopyVertices(first, count, units, &off))
	{
		return;
	}

	BindRing(off);
	rsxDrawVertexArray(ps3gl.ctx, prim, 0, (u32)count);

	ps3gl.st_draws++;
	ps3gl.st_verts += (uint32_t)count;
}

void APIENTRY
glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{
	static uint16_t idx16[65536];
	uint32_t prim, off;
	int i, units, lo = 0x7fffffff, hi = -1;

	if (count <= 0 || !indices || !Prim(mode, &prim) || !PrepareDraw(&units))
	{
		return;
	}

	if (count > 65536)
	{
		static int warned;

		if (!warned)
		{
			PS3_Log("[gl] WARNING: glDrawElements with %d indices, cut to 65536", count);
			warned = 1;
		}

		count = 65536;
	}

	/* the range of vertices used, and the indices as 16 bits */
	for (i = 0; i < count; i++)
	{
		int v;

		switch (type)
		{
			case GL_UNSIGNED_BYTE: v = ((const uint8_t *)indices)[i]; break;
			case GL_UNSIGNED_INT:  v = (int)((const uint32_t *)indices)[i]; break;
			default:               v = ((const uint16_t *)indices)[i]; break;
		}

		if (v < lo) lo = v;
		if (v > hi) hi = v;

		idx16[i] = (uint16_t)v;
	}

	if (hi - lo >= 65536)
	{
		return;
	}

	if (!CopyVertices(lo, hi - lo + 1, units, &off))
	{
		return;
	}

	if (lo)
	{
		for (i = 0; i < count; i++)
		{
			idx16[i] = (uint16_t)(idx16[i] - lo);
		}
	}

	BindRing(off);
	rsxDrawInlineIndexArray16(ps3gl.ctx, prim, 0, (u32)count, idx16);

	ps3gl.st_draws++;
	ps3gl.st_verts += (uint32_t)(hi - lo + 1);
}
