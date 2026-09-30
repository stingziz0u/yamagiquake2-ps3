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
 * Lock-on (+lockon, cl_input.c), the same idea as in ioRTWC-PS3 (itself
 * from RealRTCW): while the button is held the view turns toward an enemy
 * -- the visible one closest to the crosshair within cl_lockon_angle
 * degrees and cl_lockon_range units -- keeps it while it lives and stays
 * in sight (it may hide for a moment) and moves on to the next one when it
 * dies. The turn is smooth (cl_lockon_speed per second, a fraction of the
 * remaining angle) and the sticks still add their own. Unbound by
 * default: Customize Buttons -> "lock on".
 *
 * Single player runs the server in the same process, so the enemies come
 * from its edicts: what the game marks SVF_MONSTER and not yet
 * SVF_DEADMONSTER (every monster's die function sets it), minus the
 * insane marines (SVF_MONSTER too, but prisoners, not enemies). Line of
 * sight is the game's own visible(): a trace with MASK_OPAQUE from the
 * eye, so walls and doors block it and windows and water do not. Without
 * a local server (a demo) it does nothing.
 *
 * This file sees the server; cl_input.c sees the client. They talk
 * through the two functions in ps3_input.h.
 *
 * =======================================================================
 */

#include "../../server/header/server.h"
#include "ps3_input.h"

#define LOCK_KEEP_MS    400     /* a locked target may hide this long */
#define LOCK_KEEP_ANGLE 60.0f   /* and drift this far off the crosshair */

static cvar_t *cl_lockon_speed;
static cvar_t *cl_lockon_angle;
static cvar_t *cl_lockon_range;

static int lock_target = -1;    /* edict number, -1 = none */
static int lock_seen_time;      /* Sys_Milliseconds() it was last visible */

static qboolean
LockOn_IsEnemy(edict_t *ent)
{
	const char *model;

	if (!ent->inuse || !(ent->svflags & SVF_MONSTER) ||
		(ent->svflags & SVF_DEADMONSTER) || ent->solid == SOLID_NOT)
	{
		return false;
	}

	if (ent->s.modelindex > 0 && ent->s.modelindex < MAX_MODELS)
	{
		model = sv.configstrings[CS_MODELS + ent->s.modelindex];

		if (strstr(model, "/insane/"))
		{
			return false;
		}
	}

	return true;
}

/* a bit above the middle of its box: the chest of a Quake II monster */
static void
LockOn_Point(const edict_t *ent, vec3_t out)
{
	out[0] = ent->s.origin[0] + (ent->mins[0] + ent->maxs[0]) * 0.5f;
	out[1] = ent->s.origin[1] + (ent->mins[1] + ent->maxs[1]) * 0.5f;
	out[2] = ent->s.origin[2] + ent->mins[2] +
		(ent->maxs[2] - ent->mins[2]) * 0.6f;
}

static qboolean
LockOn_Visible(const vec3_t eye, const vec3_t point, const edict_t *player,
		const edict_t *target)
{
	trace_t tr;

	tr = SV_Trace(eye, vec3_origin, vec3_origin, point, player, MASK_OPAQUE);

	return !tr.allsolid && (tr.fraction >= 0.999f || tr.ent == target);
}

static float
LockOn_AngleDelta(float a)
{
	a = fmodf(a, 360.0f);

	if (a > 180.0f)
	{
		a -= 360.0f;
	}
	else if (a < -180.0f)
	{
		a += 360.0f;
	}

	return a;
}

/* The turn from the view to the point, and how far off it is. Quake II's
   pitch is positive looking down. */
static float
LockOn_Delta(const vec3_t eye, const vec3_t point, const vec3_t view,
		float *dyaw, float *dpitch)
{
	float dx = point[0] - eye[0];
	float dy = point[1] - eye[1];
	float dz = point[2] - eye[2];
	float yaw = atan2f(dy, dx) * (180.0f / M_PI);
	float pitch = -atan2f(dz, sqrtf(dx * dx + dy * dy)) * (180.0f / M_PI);

	*dyaw = LockOn_AngleDelta(yaw - view[YAW]);
	*dpitch = LockOn_AngleDelta(pitch - view[PITCH]);

	return sqrtf(*dyaw * *dyaw + *dpitch * *dpitch);
}

void
PS3_LockOn_Reset(void)
{
	lock_target = -1;
}

int
PS3_LockOn_Aim(int playernum, const float *eye, const float *view,
		float dt, float *turn_yaw, float *turn_pitch)
{
	edict_t *player, *ent, *best = NULL;
	vec3_t point;
	float dyaw, dpitch, off, frac, best_score = 1e9f;
	int i, now;

	if (!cl_lockon_speed)
	{
		cl_lockon_speed = Cvar_Get("cl_lockon_speed", "12", CVAR_ARCHIVE);
		cl_lockon_angle = Cvar_Get("cl_lockon_angle", "30", CVAR_ARCHIVE);
		cl_lockon_range = Cvar_Get("cl_lockon_range", "3000", CVAR_ARCHIVE);
	}

	if (sv.state != ss_game || !ge || playernum < 0 ||
		playernum + 1 >= ge->num_edicts)
	{
		lock_target = -1;
		return 0;
	}

	player = EDICT_NUM(playernum + 1);
	now = Sys_Milliseconds();

	/* keep the locked one while it lives and has not been out of sight long */
	if (lock_target > 0 && lock_target < ge->num_edicts)
	{
		ent = EDICT_NUM(lock_target);

		if (LockOn_IsEnemy(ent))
		{
			LockOn_Point(ent, point);
			off = LockOn_Delta(eye, point, view, &dyaw, &dpitch);

			if (LockOn_Visible(eye, point, player, ent))
			{
				lock_seen_time = now;
			}

			if (off <= LOCK_KEEP_ANGLE && now - lock_seen_time <= LOCK_KEEP_MS)
			{
				best = ent;
			}
		}
	}

	/* or take the one closest to the crosshair */
	if (!best)
	{
		lock_target = -1;

		for (i = 1; i < ge->num_edicts; i++)
		{
			vec3_t d;
			float dist, score;

			ent = EDICT_NUM(i);

			if (ent == player || !LockOn_IsEnemy(ent))
			{
				continue;
			}

			LockOn_Point(ent, point);
			VectorSubtract(point, eye, d);
			dist = VectorLength(d);

			if (dist > cl_lockon_range->value)
			{
				continue;
			}

			off = LockOn_Delta(eye, point, view, &dyaw, &dpitch);

			if (off > cl_lockon_angle->value)
			{
				continue;
			}

			score = off + dist * 0.005f; /* 200 units farther weigh like 1 degree */

			if (score >= best_score || !LockOn_Visible(eye, point, player, ent))
			{
				continue;
			}

			best_score = score;
			best = ent;
			lock_target = i;
		}

		if (!best)
		{
			return 0;
		}

		lock_seen_time = now;
	}

	/* turn a fraction of the way, the same per second at any frame rate */
	LockOn_Point(best, point);
	LockOn_Delta(eye, point, view, &dyaw, &dpitch);

	if (dt > 0.1f)
	{
		dt = 0.1f;
	}

	frac = 1.0f - expf(-cl_lockon_speed->value * dt);
	*turn_yaw = dyaw * frac;
	*turn_pitch = dpitch * frac;

	return 1;
}
