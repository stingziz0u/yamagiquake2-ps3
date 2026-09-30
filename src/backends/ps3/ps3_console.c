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
 * The console with the pad: SELECT + TRIANGLE and the XMB on-screen
 * keyboard (OSK).
 *
 *   SELECT + TRIANGLE  opens / closes the console (anywhere)
 *   CROSS              opens the XMB keyboard with the current line; what
 *                      is accepted there runs as a command
 *   TRIANGLE           runs the current line as it is
 *   SQUARE             command completion (Tab)
 *   CIRCLE             closes the console
 *   D-pad up / down    command history, left: erase a character
 *   L1 / R1            scroll the console up / down
 *   START              the menu, as always
 *
 * SELECT is a modifier now, the same as in ioRTWC-PS3: on its own it goes
 * to the game on release (a tap) or after being held for SELECT_HOLD_MS,
 * so SELECT + TRIANGLE never triggers either button's binding ("cmd help"
 * and "inven" by default).
 *
 * The OSK recipe is the one from CrispyCell / ioRTWC-PS3, proven on the
 * console (itself from Apollo Save Tool, bucanero/apollo-ps3): a memory
 * container, a sysutil callback on slot 1 (slot 0 is the game's exit
 * callback, system.c), oskLoadAsync, and a state machine driven by that
 * callback -- PS3_Pump() already runs sysUtilCheckCallback() every frame
 * -- while the game keeps drawing underneath. While the OSK is up the pad
 * is not read for the game, and the buttons still held when it closes
 * (the X that accepted) are ignored until released.
 *
 * sdl_ps3.c runs the pad's buttons through PS3_ConsolePad_Filter() before
 * turning them into SDL events.
 *
 * =======================================================================
 */

#include <string.h>

#include <ppu-types.h>
#include <sys/memory.h>
#include <sysutil/sysutil.h>
#include <sysutil/osk.h>

#include "../../client/header/client.h"

#include "ps3_platform.h"
#include "ps3_pad.h"
#include "ps3_input.h"

#define SELECT_HOLD_MS 250

/* cl_keyboard.c, cl_console.c */
extern char key_lines[NUM_KEY_LINES][MAXCMDLINE];
extern int key_linepos;
extern int edit_line;
void Key_ClearTyping(void);

/* ================================================================ */
/* The XMB keyboard                                                   */
/* ================================================================ */

#define OSK_MAX_CHARS       255
#define OSK_CONTAINER_BIG   (8 * 1024 * 1024)   /* CrispyCell, proven */
#define OSK_CONTAINER_SMALL (4 * 1024 * 1024)   /* "typical" per osk.h */

enum { OSK_IDLE, OSK_LOADING, OSK_OPEN, OSK_UNLOADING };

static int osk_state = OSK_IDLE;
static int osk_accepted;
static int osk_just_closed;
static int osk_result_ready;
static char osk_result[OSK_MAX_CHARS + 1];
static sys_mem_container_t osk_container;
static u16 osk_title[32];
static u16 osk_start[OSK_MAX_CHARS + 1];
static u16 osk_text[OSK_MAX_CHARS + 1];
static oskCallbackReturnParam osk_ret;

int
PS3_OSK_Active(void)
{
	return osk_state != OSK_IDLE;
}

static void
OSK_AsciiToUtf16(const char *in, u16 *out, int max)
{
	int i;

	for (i = 0; in[i] && i < max; i++)
	{
		out[i] = (u16)(unsigned char)in[i];
	}

	out[i] = 0;
}

/* The full-width panel writes U+FF01..U+FF5E; U+3000 is its space. Unlike
   the save names of the other ports, quotes and semicolons are kept: the
   console needs them (bind x "a; b"). */
static void
OSK_Utf16ToAscii(const u16 *in, int len, char *out, int size)
{
	int i, n = 0;

	for (i = 0; i < len && in[i] && n < size - 1; i++)
	{
		u16 c = in[i];

		if (c >= 0xFF01 && c <= 0xFF5E)
		{
			c -= 0xFEE0;
		}
		else if (c == 0x3000)
		{
			c = ' ';
		}

		if (c >= 32 && c < 127)
		{
			out[n++] = (char)c;
		}
	}

	out[n] = 0;
}

static void
OSK_Finish(void)
{
	sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT1);

	if (osk_container)
	{
		sysMemContainerDestroy(osk_container);
		osk_container = 0;
	}

	osk_state = OSK_IDLE;
	osk_just_closed = 1;
}

static void
OSK_Callback(u64 status, u64 param, void *userdata)
{
	(void)param;
	(void)userdata;

	switch (status)
	{
		case SYSUTIL_OSK_LOADED:
			osk_state = OSK_OPEN;
			break;

		case SYSUTIL_OSK_INPUT_ENTERED:
			osk_accepted = 1;
			oskGetInputText(&osk_ret);
			break;

		case SYSUTIL_OSK_INPUT_CANCELED:
			osk_accepted = 0;
			oskAbort();
			osk_state = OSK_UNLOADING;
			oskUnloadAsync(&osk_ret);
			break;

		case SYSUTIL_OSK_DONE:
			if (osk_state != OSK_UNLOADING)
			{
				osk_state = OSK_UNLOADING;
				oskUnloadAsync(&osk_ret);
			}
			break;

		case SYSUTIL_OSK_UNLOADED:
			if (osk_accepted || osk_ret.res == OSK_OK || osk_ret.res == OSK_NO_TEXT)
			{
				OSK_Utf16ToAscii(osk_text, OSK_MAX_CHARS, osk_result, sizeof(osk_result));
				osk_result_ready = 1;
				PS3_Log("[osk] accepted \"%s\"", osk_result);
			}
			else
			{
				PS3_Log("[osk] canceled");
			}

			OSK_Finish();
			break;

		default:
			break;
	}
}

/* Opens the keyboard with the console's current line. Returns 0 if it
   could not open (no memory for the dialog): the console stays as it was. */
static int
OSK_Open(void)
{
	oskParam param;
	oskInputFieldInfo info;

	if (osk_state != OSK_IDLE)
	{
		return 1;
	}

	if (sysMemContainerCreate(&osk_container, OSK_CONTAINER_BIG) != 0 &&
		sysMemContainerCreate(&osk_container, OSK_CONTAINER_SMALL) != 0)
	{
		osk_container = 0;
		PS3_Log("[osk] no memory for the keyboard");
		return 0;
	}

	OSK_AsciiToUtf16("Console", osk_title, 31);
	OSK_AsciiToUtf16(key_lines[edit_line], osk_start, OSK_MAX_CHARS);
	memset(osk_text, 0, sizeof(osk_text));
	memset(&osk_ret, 0, sizeof(osk_ret));
	osk_ret.len = OSK_MAX_CHARS;
	osk_ret.str = osk_text;
	osk_accepted = 0;
	osk_result_ready = 0;

	memset(&param, 0, sizeof(param));
	param.firstViewPanel = OSK_PANEL_TYPE_ALPHABET_FULL_WIDTH;
	param.allowedPanels = OSK_PANEL_TYPE_ALPHABET | OSK_PANEL_TYPE_NUMERAL |
		OSK_PANEL_TYPE_ENGLISH | OSK_PANEL_TYPE_URL;
	param.prohibitFlags = OSK_PROHIBIT_RETURN;
	param.controlPoint.x = 0;
	param.controlPoint.y = 0;

	memset(&info, 0, sizeof(info));
	info.message = osk_title;
	info.startText = osk_start;
	info.maxLength = OSK_MAX_CHARS;

	oskSetKeyLayoutOption(OSK_10KEY_PANEL | OSK_FULLKEY_PANEL);
	oskAddSupportLanguage(OSK_PANEL_TYPE_ALPHABET | OSK_PANEL_TYPE_ENGLISH);
	oskSetLayoutMode(OSK_LAYOUTMODE_HORIZONTAL_ALIGN_CENTER |
		OSK_LAYOUTMODE_VERTICAL_ALIGN_CENTER);
	oskSetInitialInputDevice(OSK_DEVICE_PAD);

	sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT1, OSK_Callback, NULL);

	if (oskLoadAsync(osk_container, &param, &info) != 0)
	{
		PS3_Log("[osk] oskLoadAsync failed");
		OSK_Finish();
		return 0;
	}

	osk_state = OSK_LOADING;
	PS3_Log("[osk] open for the console");

	return 1;
}

/* Leaving the game with the keyboard still up (Quit Game from the XMB):
   nothing of ours may stay registered with the system (handoff notes
   3.64). */
void
PS3_OSK_Shutdown(void)
{
	if (osk_state == OSK_IDLE)
	{
		return;
	}

	oskAbort();
	sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT1);
	osk_state = OSK_IDLE;
}

/* ================================================================ */
/* The pad while the console is open                                  */
/* ================================================================ */

static void
Console_Key(int key)
{
	Key_Event(key, true, true);
	Key_Event(key, false, true);
}

/* Con_ToggleConsole_f() starts the demo loop when there is no game
   (disconnected, the menu at boot): there the console just replaces the
   menu and goes back to it. */
static void
Console_Toggle(void)
{
	if (cls.state == ca_disconnected && !cl.attractloop)
	{
		if (cls.key_dest == key_console)
		{
			M_Menu_Main_f();
		}
		else
		{
			M_ForceMenuOff();
			Key_ClearTyping();
			cls.key_dest = key_console;
		}

		return;
	}

	Con_ToggleConsole_f();
}

/* The accepted text becomes the console line and runs, the same as typing
   it and pressing Enter. */
static void
Console_RunOskResult(void)
{
	osk_result_ready = 0;

	if (cls.key_dest != key_console)
	{
		return;
	}

	Q_strlcpy(key_lines[edit_line], osk_result, MAXCMDLINE);
	key_linepos = (int)strlen(key_lines[edit_line]);

	if (osk_result[0])
	{
		Console_Key(K_ENTER);
	}
}

unsigned
PS3_ConsolePad_Filter(unsigned raw)
{
	/* SELECT not held / held, not given to the game yet / held and given
	   to the game / used for SELECT + TRIANGLE */
	enum { SEL_UP, SEL_PENDING, SEL_SENT, SEL_COMBO };
	static unsigned prev, swallow;
	static int select_state = SEL_UP;
	static int select_at;
	unsigned pressed, out;
	int select_out = 0;

	pressed = raw & ~prev;
	prev = raw;
	swallow &= raw;   /* released buttons count again */

	/* The keyboard has the pad; what is still held when it closes is not
	   a new press. */
	if (osk_state != OSK_IDLE || osk_just_closed)
	{
		osk_just_closed = 0;
		swallow |= raw;
		select_state = (raw & PS3_PAD_SELECT) ? SEL_COMBO : SEL_UP;
		return 0;
	}

	if (osk_result_ready)
	{
		Console_RunOskResult();
	}

	/* SELECT + TRIANGLE: the console */
	if (raw & PS3_PAD_SELECT)
	{
		if (pressed & PS3_PAD_SELECT)
		{
			select_state = SEL_PENDING;
			select_at = Sys_Milliseconds();
		}

		if ((pressed & PS3_PAD_TRIANGLE) && select_state != SEL_UP)
		{
			Console_Toggle();
			swallow |= PS3_PAD_TRIANGLE;
			pressed &= ~PS3_PAD_TRIANGLE;

			if (select_state == SEL_PENDING)
			{
				select_state = SEL_COMBO;
			}
		}

		if (select_state == SEL_PENDING &&
			Sys_Milliseconds() - select_at >= SELECT_HOLD_MS)
		{
			select_state = SEL_SENT;
		}

		select_out = (select_state == SEL_SENT);
	}
	else
	{
		/* a tap: pressed now, released on the next frame */
		select_out = (select_state == SEL_PENDING);
		select_state = SEL_UP;
	}

	if (cls.key_dest == key_console)
	{
		pressed &= ~swallow;

		if (pressed & PS3_PAD_CROSS)
		{
			if (!OSK_Open())
			{
				Console_Key(K_ENTER);
			}
		}

		if (pressed & PS3_PAD_TRIANGLE)
		{
			Console_Key(K_ENTER);
		}

		if (pressed & PS3_PAD_SQUARE)
		{
			Console_Key(K_TAB);
		}

		if (pressed & PS3_PAD_CIRCLE)
		{
			Console_Toggle();
		}

		if (pressed & PS3_PAD_UP)
		{
			Console_Key(K_UPARROW);
		}

		if (pressed & PS3_PAD_DOWN)
		{
			Console_Key(K_DOWNARROW);
		}

		if (pressed & PS3_PAD_LEFT)
		{
			Console_Key(K_BACKSPACE);
		}

		if (pressed & PS3_PAD_L1)
		{
			Console_Key(K_PGUP);
		}

		if (pressed & PS3_PAD_R1)
		{
			Console_Key(K_PGDN);
		}

		/* Nothing else reaches the game while the console is open (its
		   bindings would run: Key_Event sends non-console keys to the
		   command interpreter even here), and nothing held now counts as
		   a press when it closes. START still opens the menu. */
		swallow |= raw & ~PS3_PAD_START;

		return raw & PS3_PAD_START;
	}

	out = raw & ~swallow & ~PS3_PAD_SELECT;

	if (select_out)
	{
		out |= PS3_PAD_SELECT;
	}

	return out;
}
