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
 * Implementation of the small "SDL 2" in compat/SDL2/SDL.h (read its
 * header comment first). Only what Yamagi's software renderer, SDL input
 * backend and SDL sound backend call.
 *
 *  - Video: the "texture" is the RSX source image (ps3_video.c); locking
 *    it hands out RSX memory, presenting it is the scaled blit + flip.
 *  - Input: one game controller, always present, fed from ioPad
 *    (ps3_pad.c). Button and axis changes become SDL controller events.
 *  - Audio: SDL's callback audio on the PS3's audio port, fed by a
 *    thread woken once per block (see the audio section).
 *
 * =======================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <lv2/systime.h>

#include <SDL2/SDL.h>

#include "../../common/header/common.h"
#include "ps3_platform.h"
#include "ps3_video.h"
#include "ps3_pad.h"

static Uint32 inited;
static const char *last_error = "";

/* ================================================================ */
/* Core                                                               */
/* ================================================================ */

void
SDL_GetVersion(SDL_version *ver)
{
	SDL_VERSION(ver);
}

int
SDL_Init(Uint32 flags)
{
	if (flags & SDL_INIT_GAMECONTROLLER)
	{
		if (!(inited & SDL_INIT_GAMECONTROLLER) && PS3_Pad_Init() != 0)
		{
			last_error = "ioPadInit failed";
			return -1;
		}
	}

	inited |= flags;

	return 0;
}

Uint32
SDL_WasInit(Uint32 flags)
{
	return inited & flags;
}

void
SDL_QuitSubSystem(Uint32 flags)
{
	if ((flags & SDL_INIT_GAMECONTROLLER) && (inited & SDL_INIT_GAMECONTROLLER))
	{
		PS3_Pad_Shutdown();
	}

	inited &= ~flags;
}

void
SDL_Quit(void)
{
	SDL_QuitSubSystem(inited);
}

const char *
SDL_GetError(void)
{
	return last_error;
}

SDL_bool
SDL_SetHint(const char *name, const char *value)
{
	(void)name;
	(void)value;

	return SDL_TRUE;
}

void
SDL_free(void *mem)
{
	free(mem);
}

Uint32
SDL_GetTicks(void)
{
	static u64 start;
	u64 now = sysGetSystemTime();   /* microseconds */

	if (start == 0)
	{
		start = now;
	}

	return (Uint32)((now - start) / 1000);
}

void
SDL_Delay(Uint32 ms)
{
	usleep(ms * 1000);
}

/* ================================================================ */
/* Video                                                              */
/* ================================================================ */

struct SDL_Renderer
{
	int dummy;
};

struct SDL_Texture
{
	int w, h;
};

static struct SDL_Renderer the_renderer;
static struct SDL_Texture the_texture;
static int texture_copied;
static u64 lock_time;   /* first lock of the frame, for profiling */

void GLimp_PS3_ApplyVideoCvars(void);   /* glimp_ps3.c */

void
SDL_SetWindowTitle(SDL_Window *window, const char *title)
{
	(void)window;
	PS3_Log("[video] %s", title);
}

Uint32
SDL_GetWindowFlags(SDL_Window *window)
{
	(void)window;

	return 0;
}

SDL_Renderer *
SDL_CreateRenderer(SDL_Window *window, int index, Uint32 flags)
{
	(void)window;
	(void)index;
	(void)flags;

	return PS3_Video_Ready() ? &the_renderer : NULL;
}

void
SDL_DestroyRenderer(SDL_Renderer *renderer)
{
	(void)renderer;
}

int
SDL_SetRenderDrawColor(SDL_Renderer *renderer, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
	return 0;
}

int
SDL_RenderClear(SDL_Renderer *renderer)
{
	return 0;
}

int
SDL_RenderCopy(SDL_Renderer *renderer, SDL_Texture *texture,
		const SDL_Rect *srcrect, const SDL_Rect *dstrect)
{
	texture_copied = 1;

	if (lock_time)
	{
		PS3_Video_ProfileCopy((long long)(sysGetSystemTime() - lock_time));
		lock_time = 0;
	}

	return 0;
}

void
SDL_RenderPresent(SDL_Renderer *renderer)
{
	(void)renderer;

	/* The renderer also presents an empty renderer once at init: only
	   frames that copied the texture reach the screen. */
	if (!texture_copied)
	{
		static int logged;

		if (logged++ < 3)
		{
			PS3_Log("[video] SDL_RenderPresent without a texture copy");
		}

		return;
	}

	texture_copied = 0;

	GLimp_PS3_ApplyVideoCvars();

	PS3_Video_Present();
}

int
SDL_GetRendererOutputSize(SDL_Renderer *renderer, int *w, int *h)
{
	*w = the_texture.w;
	*h = the_texture.h;

	return 0;
}

SDL_Texture *
SDL_CreateTexture(SDL_Renderer *renderer, Uint32 format, int access, int w, int h)
{
	/* The software renderer asks for BGRA8888 on big-endian machines, but
	   its palette is built in A8R8G8B8 order on the PS3 (sw_main.c), which
	   is what the RSX scales. */
	if (PS3_Video_SetSource(w, h) != 0)
	{
		last_error = "can't allocate the source image in RSX memory";
		return NULL;
	}

	the_texture.w = w;
	the_texture.h = h;
	texture_copied = 0;

	return &the_texture;
}

void
SDL_DestroyTexture(SDL_Texture *texture)
{
	(void)texture;
	PS3_Video_FreeSource();
}

int
SDL_LockTexture(SDL_Texture *texture, const SDL_Rect *rect, void **pixels, int *pitch)
{
	Uint8 *base = (Uint8 *)PS3_Video_Source();

	if (!base)
	{
		last_error = "no source image";
		return -1;
	}

	*pitch = texture->w * 4;

	if (!lock_time)
	{
		lock_time = sysGetSystemTime();
	}

	if (rect)
	{
		base += rect->y * texture->w * 4 + rect->x * 4;
	}

	*pixels = base;

	return 0;
}

void
SDL_UnlockTexture(SDL_Texture *texture)
{
	(void)texture;
}

/* ================================================================ */
/* Keyboard / clipboard (no USB keyboard support yet)                */
/* ================================================================ */

SDL_Keymod
SDL_GetModState(void)
{
	return KMOD_NONE;
}

void
SDL_StartTextInput(void)
{
}

char *
SDL_GetClipboardText(void)
{
	char *s = malloc(1);

	if (s)
	{
		s[0] = '\0';
	}

	return s;
}

int
SDL_SetClipboardText(const char *text)
{
	(void)text;

	return -1;
}

/* ================================================================ */
/* Game controller: the DualShock 3                                   */
/* ================================================================ */

struct _SDL_Joystick
{
	int dummy;
};

struct _SDL_GameController
{
	int dummy;
};

static struct _SDL_Joystick the_joystick;
static struct _SDL_GameController the_controller;
static int controller_open;

#define EVENT_QUEUE 64

static SDL_Event queue[EVENT_QUEUE];
static int queue_head, queue_count;
static int polled;

static unsigned prev_buttons;
static Sint16 prev_axis[SDL_CONTROLLER_AXIS_MAX];
static Uint32 rumble_until;
static int rumbling;

static const struct
{
	unsigned mask;
	Uint8 button;
} button_map[] = {
	{PS3_PAD_CROSS, SDL_CONTROLLER_BUTTON_A},       /* south */
	{PS3_PAD_CIRCLE, SDL_CONTROLLER_BUTTON_B},      /* east */
	{PS3_PAD_SQUARE, SDL_CONTROLLER_BUTTON_X},      /* west */
	{PS3_PAD_TRIANGLE, SDL_CONTROLLER_BUTTON_Y},    /* north */
	{PS3_PAD_SELECT, SDL_CONTROLLER_BUTTON_BACK},
	{PS3_PAD_START, SDL_CONTROLLER_BUTTON_START},
	{PS3_PAD_L3, SDL_CONTROLLER_BUTTON_LEFTSTICK},
	{PS3_PAD_R3, SDL_CONTROLLER_BUTTON_RIGHTSTICK},
	{PS3_PAD_L1, SDL_CONTROLLER_BUTTON_LEFTSHOULDER},
	{PS3_PAD_R1, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER},
	{PS3_PAD_UP, SDL_CONTROLLER_BUTTON_DPAD_UP},
	{PS3_PAD_DOWN, SDL_CONTROLLER_BUTTON_DPAD_DOWN},
	{PS3_PAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_LEFT},
	{PS3_PAD_RIGHT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT},
};

static void
PushEvent(const SDL_Event *ev)
{
	if (queue_count >= EVENT_QUEUE)
	{
		return;   /* can't happen with one pad; drop rather than overrun */
	}

	queue[(queue_head + queue_count) % EVENT_QUEUE] = *ev;
	queue_count++;
}

/* 0..255 (128 = center) -> -32768..32767, same direction as SDL (y down) */
static Sint16
StickToAxis(int raw)
{
	int v = (raw - 128) * 256;

	if (raw >= 255)
	{
		v = 32767;
	}

	if (v < -32768)
	{
		v = -32768;
	}

	return (Sint16)v;
}

static void
AxisEvent(int axis, Sint16 value)
{
	SDL_Event ev;

	if (value == prev_axis[axis])
	{
		return;
	}

	prev_axis[axis] = value;

	memset(&ev, 0, sizeof(ev));
	ev.type = SDL_CONTROLLERAXISMOTION;
	ev.caxis.which = 0;
	ev.caxis.axis = (Uint8)axis;
	ev.caxis.value = value;
	PushEvent(&ev);
}

static void
PollController(void)
{
	ps3_padstate_t st;
	unsigned changed;
	size_t i;

	/* once per frame: sysutil queue ("Quit Game" from the XMB) */
	PS3_Pump();

	if (!(inited & SDL_INIT_GAMECONTROLLER))
	{
		return;
	}

	PS3_Pad_Poll(&st);

	if (rumbling && (Sint32)(SDL_GetTicks() - rumble_until) >= 0)
	{
		PS3_Pad_Rumble(0, 0);
		rumbling = 0;
	}

	if (!controller_open)
	{
		prev_buttons = st.buttons;
		return;
	}

	changed = st.buttons ^ prev_buttons;

	for (i = 0; i < sizeof(button_map) / sizeof(button_map[0]); i++)
	{
		if (changed & button_map[i].mask)
		{
			SDL_Event ev;

			memset(&ev, 0, sizeof(ev));
			ev.type = (st.buttons & button_map[i].mask) ?
				SDL_CONTROLLERBUTTONDOWN : SDL_CONTROLLERBUTTONUP;
			ev.cbutton.which = 0;
			ev.cbutton.button = button_map[i].button;
			ev.cbutton.state = (ev.type == SDL_CONTROLLERBUTTONDOWN);
			PushEvent(&ev);
		}
	}

	prev_buttons = st.buttons;

	/* L2/R2 are read as digital buttons: full or nothing. */
	AxisEvent(SDL_CONTROLLER_AXIS_TRIGGERLEFT, (st.buttons & PS3_PAD_L2) ? 32767 : 0);
	AxisEvent(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, (st.buttons & PS3_PAD_R2) ? 32767 : 0);

	AxisEvent(SDL_CONTROLLER_AXIS_LEFTX, StickToAxis(st.lx));
	AxisEvent(SDL_CONTROLLER_AXIS_LEFTY, StickToAxis(st.ly));
	AxisEvent(SDL_CONTROLLER_AXIS_RIGHTX, StickToAxis(st.rx));
	AxisEvent(SDL_CONTROLLER_AXIS_RIGHTY, StickToAxis(st.ry));
}

int
SDL_PollEvent(SDL_Event *event)
{
	/* IN_Update() drains the queue every frame; poll the pad once when
	   it starts. */
	if (queue_count == 0 && !polled)
	{
		PollController();
		polled = 1;
	}

	if (queue_count == 0)
	{
		polled = 0;
		return 0;
	}

	*event = queue[queue_head];
	queue_head = (queue_head + 1) % EVENT_QUEUE;
	queue_count--;

	return 1;
}

void
SDL_FlushEvents(Uint32 minType, Uint32 maxType)
{
	queue_count = 0;
	queue_head = 0;
}

int
SDL_NumJoysticks(void)
{
	/* The pad is always "there": ioPad handles plugging and unplugging
	   underneath (ps3_pad.c), and a disconnected pad just rests. */
	return 1;
}

SDL_Joystick *
SDL_JoystickOpen(int device_index)
{
	return device_index == 0 ? &the_joystick : NULL;
}

void
SDL_JoystickClose(SDL_Joystick *joystick)
{
}

const char *
SDL_JoystickName(SDL_Joystick *joystick)
{
	return "PLAYSTATION(R)3 Controller";
}

int
SDL_JoystickNumButtons(SDL_Joystick *joystick)
{
	return 17;
}

int
SDL_JoystickNumAxes(SDL_Joystick *joystick)
{
	return 6;
}

int
SDL_JoystickNumHats(SDL_Joystick *joystick)
{
	return 0;
}

SDL_JoystickID
SDL_JoystickInstanceID(SDL_Joystick *joystick)
{
	return 0;
}

SDL_JoystickGUID
SDL_JoystickGetDeviceGUID(int device_index)
{
	SDL_JoystickGUID guid;

	memset(&guid, 0, sizeof(guid));

	return guid;
}

void
SDL_JoystickGetGUIDString(SDL_JoystickGUID guid, char *psz, int cb)
{
	if (cb > 0)
	{
		Q_strlcpy(psz, "00000000000000000000000000000000", cb);
	}
}

SDL_bool
SDL_IsGameController(int joystick_index)
{
	return joystick_index == 0 ? SDL_TRUE : SDL_FALSE;
}

SDL_GameController *
SDL_GameControllerOpen(int joystick_index)
{
	ps3_padstate_t st;
	int i;

	if (joystick_index != 0)
	{
		return NULL;
	}

	/* Start from the current state: buttons already held when the game
	   opens the controller (START from the XMB...) must not fire. */
	PS3_Pad_Poll(&st);
	prev_buttons = st.buttons;

	for (i = 0; i < SDL_CONTROLLER_AXIS_MAX; i++)
	{
		prev_axis[i] = 0;
	}

	controller_open = 1;

	return &the_controller;
}

void
SDL_GameControllerClose(SDL_GameController *gamecontroller)
{
	controller_open = 0;
	PS3_Pad_Rumble(0, 0);
	rumbling = 0;
}

SDL_Joystick *
SDL_GameControllerGetJoystick(SDL_GameController *gamecontroller)
{
	return &the_joystick;
}

SDL_GameControllerType
SDL_GameControllerGetType(SDL_GameController *gamecontroller)
{
	/* PlayStation button labels in the menus */
	return SDL_CONTROLLER_TYPE_PS3;
}

char *
SDL_GameControllerMapping(SDL_GameController *gamecontroller)
{
	/* SDL returns a malloc()ed string that Yamagi never frees */
	static char mapping[] = "DualShock 3 through ioPad (native)";

	return mapping;
}

int
SDL_GameControllerAddMappingsFromFile(const char *file)
{
	return 0;
}

SDL_bool
SDL_GameControllerHasSensor(SDL_GameController *gamecontroller, SDL_SensorType type)
{
	return SDL_FALSE;
}

int
SDL_GameControllerSetSensorEnabled(SDL_GameController *gamecontroller,
		SDL_SensorType type, SDL_bool enabled)
{
	return -1;
}

float
SDL_GameControllerGetSensorDataRate(SDL_GameController *gamecontroller, SDL_SensorType type)
{
	return 0.0f;
}

SDL_bool
SDL_GameControllerHasLED(SDL_GameController *gamecontroller)
{
	return SDL_FALSE;
}

int
SDL_GameControllerSetLED(SDL_GameController *gamecontroller, Uint8 red, Uint8 green, Uint8 blue)
{
	return -1;
}

SDL_bool
SDL_GameControllerHasRumble(SDL_GameController *gamecontroller)
{
	/* Rumble is off on the PS3 build: saying the pad has none also hides
	   "rumble intensity" in the gamepad menu (input/sdl2.c). */
	return SDL_FALSE;
}

int
SDL_GameControllerRumble(SDL_GameController *gamecontroller,
		Uint16 low_frequency_rumble, Uint16 high_frequency_rumble, Uint32 duration_ms)
{
	return -1;
}

/* ================================================================ */
/* Haptic: never available                                            */
/* ================================================================ */

SDL_Haptic *
SDL_HapticOpenFromJoystick(SDL_Joystick *joystick)
{
	return NULL;
}

SDL_Haptic *
SDL_HapticOpenFromMouse(void)
{
	return NULL;
}

void
SDL_HapticClose(SDL_Haptic *haptic)
{
}

unsigned int
SDL_HapticQuery(SDL_Haptic *haptic)
{
	return 0;
}

int
SDL_HapticNumEffects(SDL_Haptic *haptic)
{
	return 0;
}

int
SDL_HapticNumEffectsPlaying(SDL_Haptic *haptic)
{
	return 0;
}

int
SDL_HapticNumAxes(SDL_Haptic *haptic)
{
	return 0;
}

int
SDL_HapticNewEffect(SDL_Haptic *haptic, SDL_HapticEffect *effect)
{
	return -1;
}

int
SDL_HapticRunEffect(SDL_Haptic *haptic, int effect, Uint32 iterations)
{
	return -1;
}

void
SDL_HapticDestroyEffect(SDL_Haptic *haptic, int effect)
{
}

/* ================================================================ */
/* Audio                                                              */
/* ================================================================ */

/*
 * SDL's callback audio on the PS3's audio port (the scheme of
 * Doom64-PS3's ps3_audio.c, proven on hardware):
 *
 *  - one port, 2 channels, 16 blocks of 256 float samples, 48000 Hz (the
 *    port's only rate). SDL_OpenAudio forces 48000 Hz stereo on the
 *    caller's spec, and Yamagi's mixer works at that rate;
 *  - a thread woken by the port's notify queue (once per block played)
 *    keeps the blocks up to AUDIO_AHEAD in front of the one being played
 *    filled: it calls SDL's callback for 256 frames of 16 bit (or 8 bit)
 *    samples and converts them to float. If it ever falls behind (the
 *    port went past it) it jumps back in front instead of writing late;
 *  - the callback runs under the audio lock, as in SDL: SDL_LockAudio
 *    keeps it out while the game mixes into its buffer.
 */

#include <sys/thread.h>
#include <sys/mutex.h>
#include <sys/event_queue.h>
#include <audio/audio.h>

#define AUDIO_RATE      48000
#define AUDIO_CHANNELS  2
#define AUDIO_AHEAD     4   /* blocks, ~21 ms */

static SDL_AudioSpec audio_spec;
static int audio_open;
static volatile int audio_paused = 1;
static volatile int audio_quit;
static u32 audio_port;
static audioPortConfig audio_cfg;
static sys_event_queue_t audio_queue;
static sys_ipc_key_t audio_key;
static sys_ppu_thread_t audio_tid;
static sys_mutex_t audio_mutex;
static u32 audio_late;      /* times the thread had fallen behind */

/* one block as the callback writes it (16 bit stereo at most) */
static Sint16 audio_block[AUDIO_BLOCK_SAMPLES * AUDIO_CHANNELS];

const char *
SDL_GetCurrentAudioDriver(void)
{
	return audio_open ? "ps3 audio port" : "ps3";
}

static float *
AudioBlock(u32 index)
{
	return (float *)(u64)audio_cfg.audioDataStart +
		(u64)index * AUDIO_BLOCK_SAMPLES * audio_cfg.channelCount;
}

static void
AudioFill(float *dst)
{
	int n = AUDIO_BLOCK_SAMPLES * AUDIO_CHANNELS;
	int i;

	if (audio_paused || !audio_spec.callback)
	{
		memset(dst, 0, n * sizeof(float));
		return;
	}

	sysMutexLock(audio_mutex, 0);

	if (audio_spec.format == AUDIO_U8)
	{
		Uint8 *b = (Uint8 *)audio_block;

		audio_spec.callback(audio_spec.userdata, b, n);

		sysMutexUnlock(audio_mutex);

		for (i = 0; i < n; i++)
		{
			dst[i] = ((int)b[i] - 128) * (1.0f / 128.0f);
		}
	}
	else
	{
		audio_spec.callback(audio_spec.userdata, (Uint8 *)audio_block, n * 2);

		sysMutexUnlock(audio_mutex);

		for (i = 0; i < n; i++)
		{
			dst[i] = audio_block[i] * (1.0f / 32768.0f);
		}
	}
}

static void
AudioThread(void *arg)
{
	u32 blocks = (u32)audio_cfg.numBlocks;
	u32 next = 1;   /* next block to fill */
	sys_event_t ev;

	(void)arg;

	while (!audio_quit)
	{
		u32 playing, dist;

		/* one event per block played; the timeout only matters for quitting */
		sysEventQueueReceive(audio_queue, &ev, 20 * 1000);

		if (audio_quit)
		{
			break;
		}

		playing = (u32)(*(volatile u64 *)(u64)audio_cfg.readIndex % blocks);
		dist = (next + blocks - playing) % blocks;

		if (dist == 0 || dist > AUDIO_AHEAD + 1)
		{
			/* the port caught up with us: start again right after it */
			if (dist == 0)
			{
				audio_late++;
			}

			next = (playing + 1) % blocks;
			dist = 1;
		}

		while (dist <= AUDIO_AHEAD)
		{
			AudioFill(AudioBlock(next));
			next = (next + 1) % blocks;
			dist++;
		}
	}

	sysThreadExit(0);
}

int
SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained)
{
	audioPortParam param;
	sys_mutex_attr_t attr;
	s32 r;

	if (audio_open)
	{
		last_error = "audio already open";
		return -1;
	}

	if (!desired || !desired->callback)
	{
		last_error = "no callback";
		return -1;
	}

	/* What the port plays. The caller reads these back from its spec. */
	desired->freq = AUDIO_RATE;
	desired->channels = AUDIO_CHANNELS;

	if (desired->format != AUDIO_U8)
	{
		desired->format = AUDIO_S16SYS;
	}

	desired->samples = AUDIO_BLOCK_SAMPLES;
	desired->silence = (desired->format == AUDIO_U8) ? 0x80 : 0;
	desired->size = AUDIO_BLOCK_SAMPLES * AUDIO_CHANNELS *
		((desired->format == AUDIO_U8) ? 1 : 2);

	audio_spec = *desired;

	if (obtained)
	{
		*obtained = *desired;
	}

	memset(&attr, 0, sizeof(attr));
	attr.attr_protocol = SYS_MUTEX_PROTOCOL_PRIO;
	attr.attr_recursive = SYS_MUTEX_ATTR_NOT_RECURSIVE;
	attr.attr_pshared = SYS_MUTEX_ATTR_NOT_PSHARED;
	attr.attr_adaptive = SYS_MUTEX_ATTR_NOT_ADAPTIVE;
	strcpy(attr.name, "q2audio");

	if (sysMutexCreate(&audio_mutex, &attr) != 0)
	{
		last_error = "sysMutexCreate failed";
		return -1;
	}

	r = audioInit();

	if (r != 0)
	{
		PS3_Log("[audio] audioInit failed (%d)", (int)r);
		last_error = "audioInit failed";
		sysMutexDestroy(audio_mutex);
		return -1;
	}

	memset(&param, 0, sizeof(param));
	param.numChannels = AUDIO_PORT_2CH;
	param.numBlocks = AUDIO_BLOCK_16;
	param.attrib = 0;
	param.level = 1.0f;

	r = audioPortOpen(&param, &audio_port);

	if (r != 0)
	{
		PS3_Log("[audio] audioPortOpen failed (%d)", (int)r);
		last_error = "audioPortOpen failed";
		audioQuit();
		sysMutexDestroy(audio_mutex);
		return -1;
	}

	audioGetPortConfig(audio_port, &audio_cfg);
	audioCreateNotifyEventQueue(&audio_queue, &audio_key);
	audioSetNotifyEventQueue(audio_key);
	sysEventQueueDrain(audio_queue);
	memset((void *)(u64)audio_cfg.audioDataStart, 0, audio_cfg.portSize);

	audio_paused = 1;   /* SDL opens paused */
	audio_quit = 0;
	audio_late = 0;

	audioPortStart(audio_port);

	if (sysThreadCreate(&audio_tid, AudioThread, NULL, 200, 64 * 1024,
				THREAD_JOINABLE, "q2audio") != 0)
	{
		PS3_Log("[audio] sysThreadCreate failed");
		last_error = "can't start the audio thread";
		audioPortStop(audio_port);
		audioRemoveNotifyEventQueue(audio_key);
		audioPortClose(audio_port);
		sysEventQueueDestroy(audio_queue, 0);
		audioQuit();
		sysMutexDestroy(audio_mutex);
		return -1;
	}

	audio_open = 1;

	PS3_Log("[audio] port %u open: %u blocks, %u channels, %d Hz, %s",
			(unsigned)audio_port, (unsigned)audio_cfg.numBlocks,
			(unsigned)audio_cfg.channelCount, AUDIO_RATE,
			desired->format == AUDIO_U8 ? "8 bit" : "16 bit");

	return 0;
}

void
SDL_PauseAudio(int pause_on)
{
	audio_paused = pause_on ? 1 : 0;
}

void
SDL_LockAudio(void)
{
	if (audio_open)
	{
		sysMutexLock(audio_mutex, 0);
	}
}

void
SDL_UnlockAudio(void)
{
	if (audio_open)
	{
		sysMutexUnlock(audio_mutex);
	}
}

void
SDL_CloseAudio(void)
{
	u64 rv;

	if (!audio_open)
	{
		return;
	}

	audio_paused = 1;
	audio_quit = 1;
	sysThreadJoin(audio_tid, &rv);

	audioPortStop(audio_port);
	audioRemoveNotifyEventQueue(audio_key);
	audioPortClose(audio_port);
	sysEventQueueDestroy(audio_queue, 0);
	audioQuit();
	sysMutexDestroy(audio_mutex);

	audio_open = 0;

	PS3_Log("[audio] closed (the mixer fell behind %u times)", (unsigned)audio_late);
}
