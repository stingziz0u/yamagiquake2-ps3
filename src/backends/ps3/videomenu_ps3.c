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
 * The video menu on the PS3, replacing src/client/menu/videomenu.c. The
 * PC menu is about windows, monitors and MSAA; here there is one TV, and
 * what matters is:
 *
 *  - the renderer: OpenGL (ref_gl1 drawn by the RSX) or software (CPU);
 *  - the internal resolution, in 4:3 or 16:9. Always set as a custom mode
 *    (r_mode -1). 16:9 relies on Yamagi's "horplus" (on by default):
 *    wider view, same vertical field of view;
 *  - how the picture is put on the TV: screen fit (overscan), scaling
 *    filter, brightness and gamma (vid_ps3_*, see glimp_ps3.c; the last
 *    two are applied by the RSX, live, see ps3_video.c);
 *  - the frame rate: up to 60, or locked at 30 for an even pace
 *    (vid_ps3_fps, see ps3_video.c);
 *  - model shadows (OpenGL only): r_shadows, and gl1_stencilshadow so
 *    each spot is darkened once, without the darker overlaps.
 *
 * =======================================================================
 */

#include "../../client/header/client.h"
#include "../../client/menu/header/qmenu.h"

extern void M_ForceMenuOff(void);
extern const char *Default_MenuKey(menuframework_s *m, int key);
extern void Default_MenuDraw(menuframework_s *m);

static menuframework_s s_video_menu;

static menulist_s s_renderer_list;
static menulist_s s_mode_list;
static menuslider_s s_brightness_slider;
static menuslider_s s_gamma_slider;
static menulist_s s_fpsdisplay_list;
static menuslider_s s_fov_slider;
static menulist_s s_shadows_list;
static menuslider_s s_screenfit_slider;
static menulist_s s_filter_list;
static menulist_s s_fps_list;
static menulist_s s_hudscale_list;
static menuaction_s s_defaults_action;
static menuaction_s s_apply_action;

typedef struct
{
	const char *name;
	int width, height;
} ps3mode_t;

/* The software renderer's cost grows with the pixel count: 640x480 and
   848x480 are the sensible choices, the rest trade detail for speed. */
static const ps3mode_t ps3_modes[] = {
	{"[4:3   320x240 ]", 320, 240},
	{"[4:3   400x300 ]", 400, 300},
	{"[4:3   512x384 ]", 512, 384},
	{"[4:3   640x480 ]", 640, 480},
	{"[16:9  640x360 ]", 640, 360},
	{"[16:9  768x432 ]", 768, 432},
	{"[16:9  848x480 ]", 848, 480},   /* rows: multiples of 64 bytes */
	{"[16:9  960x540 ]", 960, 540},
	{"[16:9  1280x720]", 1280, 720},
};

#define NUM_PS3_MODES (int)(sizeof(ps3_modes) / sizeof(ps3_modes[0]))
#define DEFAULT_MODE 3      /* 640x480, software */
#define DEFAULT_MODE_GL 8   /* 1280x720, OpenGL */


/* The few standard r_mode values that matter here (vid.c's table is
   static). */
static qboolean
StandardModeSize(int mode, int *w, int *h)
{
	switch (mode)
	{
		case 0: *w = 320; *h = 240; return true;
		case 1: *w = 400; *h = 300; return true;
		case 2: *w = 512; *h = 384; return true;
		case 4: *w = 640; *h = 480; return true;
		case 14: *w = 1280; *h = 720; return true;
		default: return false;
	}
}

static int
IsGL(void)
{
	return strcmp(Cvar_VariableString("vid_renderer"), "soft") != 0;
}

static int
CurrentMode(void)
{
	int mode = (int)Cvar_VariableValue("r_mode");
	int w, h, i;

	if (mode == -1)
	{
		w = (int)Cvar_VariableValue("r_customwidth");
		h = (int)Cvar_VariableValue("r_customheight");
	}
	else if (!StandardModeSize(mode, &w, &h))
	{
		return DEFAULT_MODE;
	}

	for (i = 0; i < NUM_PS3_MODES; i++)
	{
		if (ps3_modes[i].width == w && ps3_modes[i].height == h)
		{
			return i;
		}
	}

	return DEFAULT_MODE;
}

/* cl_showfps: 0 off, 1 the average; cl_showfps_pos: 1 left, 0 right */
static void
FpsDisplayFunc(void *unused)
{
	switch (s_fpsdisplay_list.curvalue)
	{
		case 1:
			Cvar_SetValue("cl_showfps", 1);
			Cvar_SetValue("cl_showfps_pos", 1);
			break;
		case 2:
			Cvar_SetValue("cl_showfps", 1);
			Cvar_SetValue("cl_showfps_pos", 0);
			break;
		default:
			Cvar_SetValue("cl_showfps", 0);
			break;
	}
}

/* 0 off, 1 r_shadows alone, 2 r_shadows + gl1_stencilshadow. Only the
   OpenGL renderer draws them; applied right away. */
static void
ShadowsFunc(void *unused)
{
	Cvar_SetValue("r_shadows", s_shadows_list.curvalue > 0);
	Cvar_SetValue("gl1_stencilshadow", s_shadows_list.curvalue == 2);
}

static int
CurrentShadows(void)
{
	if (Cvar_VariableValue("r_shadows") == 0)
	{
		return 0;
	}

	return Cvar_VariableValue("gl1_stencilshadow") ? 2 : 1;
}

static void
ApplyChanges(void *unused)
{
	const ps3mode_t *m = &ps3_modes[s_mode_list.curvalue];
	qboolean restart = false;
	int w, h;

	/* renderer */
	if (s_renderer_list.curvalue != (IsGL() ? 0 : 1))
	{
		Cvar_Set("vid_renderer", s_renderer_list.curvalue == 0 ? "gl1" : "soft");
		restart = true;
	}

	/* resolution */
	if (CurrentMode() != s_mode_list.curvalue || Cvar_VariableValue("r_mode") != -1)
	{
		Cvar_SetValue("r_customwidth", m->width);
		Cvar_SetValue("r_customheight", m->height);
		Cvar_SetValue("r_mode", -1);
		restart = true;
	}
	else
	{
		w = (int)Cvar_VariableValue("r_customwidth");
		h = (int)Cvar_VariableValue("r_customheight");

		if (w != m->width || h != m->height)
		{
			restart = true;
		}
	}

	/* scaling filter: 0 = smooth (bilinear), 1 = sharp (nearest) */
	Cvar_SetValue("vid_ps3_filter", s_filter_list.curvalue == 0 ? 1 : 0);

	/* frame rate */
	Cvar_SetValue("vid_ps3_fps", s_fps_list.curvalue == 1 ? 30 : 0);

	/* HUD, menus, console and crosshair scale together */
	if (s_hudscale_list.curvalue == 0)
	{
		Cvar_SetValue("r_hudscale", -1);
	}
	else
	{
		Cvar_SetValue("r_hudscale", s_hudscale_list.curvalue);
	}

	Cvar_SetValue("r_consolescale", Cvar_VariableValue("r_hudscale"));
	Cvar_SetValue("r_menuscale", Cvar_VariableValue("r_hudscale"));
	Cvar_SetValue("crosshair_scale", Cvar_VariableValue("r_hudscale"));

	if (restart)
	{
		Cbuf_AddText("vid_restart\n");
	}

	M_ForceMenuOff();
}

static void
ResetDefaults(void *unused)
{
	s_renderer_list.curvalue = 0;
	s_mode_list.curvalue = DEFAULT_MODE_GL;
	Cvar_SetValue("vid_ps3_brightness", 1.0f);
	Cvar_SetValue("vid_ps3_gamma", 1.0f);
	s_fpsdisplay_list.curvalue = 0;
	FpsDisplayFunc(NULL);
	Cvar_SetValue("fov", 90);
	s_shadows_list.curvalue = 0;
	ShadowsFunc(NULL);
	Cvar_SetValue("vid_ps3_screenfit", 90);
	s_filter_list.curvalue = 0;
	s_fps_list.curvalue = 0;
	s_hudscale_list.curvalue = 0;
}

void
VID_MenuInit(void)
{
	int y = 0;
	int hudscale;

	static const char *mode_names[NUM_PS3_MODES + 1];

	static const char *renderer_names[] = {
		"opengl (gpu)",
		"software (cpu)",
		NULL
	};

	static const char *shadows_names[] = {
		"off",
		"on",
		"on (stencil)",
		NULL
	};

	static const char *filter_names[] = {
		"smooth",
		"sharp",
		NULL
	};

	static const char *fps_names[] = {
		"up to 60",
		"locked 30",
		NULL
	};

	static const char *fpsdisplay_names[] = {
		"off",
		"top left",
		"top right",
		NULL
	};

	static const char *hudscale_names[] = {
		"auto",
		"1x",
		"2x",
		"3x",
		"4x",
		NULL
	};

	for (int i = 0; i < NUM_PS3_MODES; i++)
	{
		mode_names[i] = ps3_modes[i].name;
	}

	mode_names[NUM_PS3_MODES] = NULL;

	memset(&s_video_menu, 0, sizeof(s_video_menu));
	s_video_menu.x = viddef.width * 0.50;
	s_video_menu.nitems = 0;
	s_video_menu.banner = "m_banner_video";

	s_renderer_list.generic.type = MTYPE_SPINCONTROL;
	s_renderer_list.generic.name = "renderer";
	s_renderer_list.generic.x = 0;
	s_renderer_list.generic.y = y;
	s_renderer_list.itemnames = renderer_names;
	s_renderer_list.curvalue = IsGL() ? 0 : 1;

	s_mode_list.generic.type = MTYPE_SPINCONTROL;
	s_mode_list.generic.name = "internal res";
	s_mode_list.generic.x = 0;
	s_mode_list.generic.y = (y += 10);
	s_mode_list.itemnames = mode_names;
	s_mode_list.curvalue = CurrentMode();

	s_brightness_slider.generic.type = MTYPE_SLIDER;
	s_brightness_slider.generic.name = "brightness";
	s_brightness_slider.generic.x = 0;
	s_brightness_slider.generic.y = (y += 10);
	s_brightness_slider.cvar = "vid_ps3_brightness";
	s_brightness_slider.minvalue = 0.5f;
	s_brightness_slider.maxvalue = 2.0f;
	s_brightness_slider.slidestep = 0.1f;
	s_brightness_slider.printformat = "%.1f";

	s_gamma_slider.generic.type = MTYPE_SLIDER;
	s_gamma_slider.generic.name = "gamma";
	s_gamma_slider.generic.x = 0;
	s_gamma_slider.generic.y = (y += 10);
	s_gamma_slider.cvar = "vid_ps3_gamma";
	s_gamma_slider.minvalue = 0.5f;
	s_gamma_slider.maxvalue = 2.5f;
	s_gamma_slider.slidestep = 0.1f;
	s_gamma_slider.printformat = "%.1f";

	s_fov_slider.generic.type = MTYPE_SLIDER;
	s_fov_slider.generic.name = "field of view";
	s_fov_slider.generic.x = 0;
	s_fov_slider.generic.y = (y += 10);
	s_fov_slider.cvar = "fov";
	s_fov_slider.minvalue = 60;
	s_fov_slider.maxvalue = 120;
	s_fov_slider.slidestep = 1;
	s_fov_slider.printformat = "%.0f";

	s_shadows_list.generic.type = MTYPE_SPINCONTROL;
	s_shadows_list.generic.name = "shadows";
	s_shadows_list.generic.x = 0;
	s_shadows_list.generic.y = (y += 10);
	s_shadows_list.generic.callback = ShadowsFunc;
	s_shadows_list.itemnames = shadows_names;
	s_shadows_list.curvalue = CurrentShadows();

	s_screenfit_slider.generic.type = MTYPE_SLIDER;
	s_screenfit_slider.generic.name = "tv screen fit";
	s_screenfit_slider.generic.x = 0;
	s_screenfit_slider.generic.y = (y += 20);
	s_screenfit_slider.cvar = "vid_ps3_screenfit";
	s_screenfit_slider.minvalue = 70;
	s_screenfit_slider.maxvalue = 100;
	s_screenfit_slider.slidestep = 2;
	s_screenfit_slider.printformat = "%.0f%%";

	s_filter_list.generic.type = MTYPE_SPINCONTROL;
	s_filter_list.generic.name = "scaling";
	s_filter_list.generic.x = 0;
	s_filter_list.generic.y = (y += 10);
	s_filter_list.itemnames = filter_names;
	s_filter_list.curvalue = Cvar_VariableValue("vid_ps3_filter") ? 0 : 1;

	s_fps_list.generic.type = MTYPE_SPINCONTROL;
	s_fps_list.generic.name = "frame rate";
	s_fps_list.generic.x = 0;
	s_fps_list.generic.y = (y += 10);
	s_fps_list.itemnames = fps_names;
	s_fps_list.curvalue = (Cvar_VariableValue("vid_ps3_fps") == 30) ? 1 : 0;

	hudscale = (int)Cvar_VariableValue("r_hudscale");
	s_hudscale_list.generic.type = MTYPE_SPINCONTROL;
	s_hudscale_list.generic.name = "hud scale";
	s_hudscale_list.generic.x = 0;
	s_hudscale_list.generic.y = (y += 10);
	s_hudscale_list.itemnames = hudscale_names;
	s_hudscale_list.curvalue = (hudscale >= 1 && hudscale <= 4) ? hudscale : 0;

	/* applied right away, like the sliders */
	s_fpsdisplay_list.generic.type = MTYPE_SPINCONTROL;
	s_fpsdisplay_list.generic.name = "fps display";
	s_fpsdisplay_list.generic.x = 0;
	s_fpsdisplay_list.generic.y = (y += 10);
	s_fpsdisplay_list.generic.callback = FpsDisplayFunc;
	s_fpsdisplay_list.itemnames = fpsdisplay_names;
	s_fpsdisplay_list.curvalue = (Cvar_VariableValue("cl_showfps") < 1) ? 0 :
		(Cvar_VariableValue("cl_showfps_pos") == 1) ? 1 : 2;

	s_defaults_action.generic.type = MTYPE_ACTION;
	s_defaults_action.generic.name = "reset to default";
	s_defaults_action.generic.x = 0;
	s_defaults_action.generic.y = (y += 20);
	s_defaults_action.generic.callback = ResetDefaults;

	s_apply_action.generic.type = MTYPE_ACTION;
	s_apply_action.generic.name = "apply";
	s_apply_action.generic.x = 0;
	s_apply_action.generic.y = (y += 10);
	s_apply_action.generic.callback = ApplyChanges;

	Menu_AddItem(&s_video_menu, (void *)&s_renderer_list);
	Menu_AddItem(&s_video_menu, (void *)&s_mode_list);
	Menu_AddItem(&s_video_menu, (void *)&s_brightness_slider);
	Menu_AddItem(&s_video_menu, (void *)&s_gamma_slider);
	Menu_AddItem(&s_video_menu, (void *)&s_fov_slider);
	Menu_AddItem(&s_video_menu, (void *)&s_shadows_list);
	Menu_AddItem(&s_video_menu, (void *)&s_screenfit_slider);
	Menu_AddItem(&s_video_menu, (void *)&s_filter_list);
	Menu_AddItem(&s_video_menu, (void *)&s_fps_list);
	Menu_AddItem(&s_video_menu, (void *)&s_hudscale_list);
	Menu_AddItem(&s_video_menu, (void *)&s_fpsdisplay_list);
	Menu_AddItem(&s_video_menu, (void *)&s_defaults_action);
	Menu_AddItem(&s_video_menu, (void *)&s_apply_action);

	Menu_SetStatusBar(&s_video_menu, "always scaled to 720p - lower res = faster");
	Menu_Center(&s_video_menu);
	s_video_menu.x -= 8;
}

void
M_Menu_Video_f(void)
{
	VID_MenuInit();
	s_video_menu.draw = Default_MenuDraw;
	s_video_menu.key = Default_MenuKey;

	M_PushMenu(&s_video_menu);
}
