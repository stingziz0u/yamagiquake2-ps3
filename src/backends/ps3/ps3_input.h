/*
 * Copyright (C) 2026 the Quake2PS3 contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * Lock-on (ps3_lockon.c) and the console with the pad (ps3_console.c).
 * Plain types only: ps3_lockon.c sees the server headers, the callers the
 * client ones.
 */

#ifndef PS3_INPUT_H
#define PS3_INPUT_H

/* Lock-on, called by cl_input.c every frame while +lockon is held. eye and
   view are the view origin and the full view angles (with delta_angles).
   Returns 1 and the turn to add to cl.viewangles when there is a target. */
int  PS3_LockOn_Aim(int playernum, const float *eye, const float *view,
		float dt, float *turn_yaw, float *turn_pitch);
void PS3_LockOn_Reset(void);

/* Takes the pad's buttons for this frame (PS3_PAD_* mask) and returns
   what the game should see: SELECT + TRIANGLE (console), the console's
   own pad controls and the XMB keyboard take what they use. */
unsigned PS3_ConsolePad_Filter(unsigned raw);
int  PS3_OSK_Active(void);
void PS3_OSK_Shutdown(void);

#endif
