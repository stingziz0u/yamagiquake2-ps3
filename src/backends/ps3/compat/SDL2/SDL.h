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
 * A tiny "SDL 2" for the PS3. It is NOT SDL: it declares only what four
 * Yamagi files use, and src/backends/ps3/sdl_ps3.c implements it on top of
 * the RSX, ioPad and the audio port:
 *
 *   src/client/refresh/soft/sw_main.c, sw_misc.c   window/texture/present
 *   src/client/input/sdl2.c                        events, game controller
 *   src/client/sound/sdl.c                         audio callback
 *
 * That way those files build unchanged and keep all of Yamagi's features
 * (gamepad menus, stick layouts, rumble, the sound mixer). The compat dir
 * comes first in the include path (ps3/Makefile), so <SDL2/SDL.h> lands
 * here. Types, constants and prototypes follow SDL 2.30.
 *
 * =======================================================================
 */

#ifndef PS3_SDL_SHIM_H
#define PS3_SDL_SHIM_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <alloca.h>

/* ---- basic types ---------------------------------------------------- */

typedef uint8_t Uint8;
typedef int8_t Sint8;
typedef uint16_t Uint16;
typedef int16_t Sint16;
typedef uint32_t Uint32;
typedef int32_t Sint32;
typedef uint64_t Uint64;
typedef int64_t Sint64;

typedef enum
{
	SDL_FALSE = 0,
	SDL_TRUE = 1
} SDL_bool;

#define SDL_memset memset
#define SDL_memcpy memcpy
#define SDL_stack_alloc(type, count) (type *)alloca(sizeof(type) * (count))
#define SDL_stack_free(data)

void SDL_free(void *mem);

/* ---- version -------------------------------------------------------- */

#define SDL_MAJOR_VERSION 2
#define SDL_MINOR_VERSION 30
#define SDL_PATCHLEVEL 0

typedef struct SDL_version
{
	Uint8 major;
	Uint8 minor;
	Uint8 patch;
} SDL_version;

#define SDL_VERSION(x) \
	do { \
		(x)->major = SDL_MAJOR_VERSION; \
		(x)->minor = SDL_MINOR_VERSION; \
		(x)->patch = SDL_PATCHLEVEL; \
	} while (0)

#define SDL_VERSIONNUM(X, Y, Z) ((X) * 1000 + (Y) * 100 + (Z))
#define SDL_COMPILEDVERSION SDL_VERSIONNUM(SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL)
#define SDL_VERSION_ATLEAST(X, Y, Z) (SDL_COMPILEDVERSION >= SDL_VERSIONNUM(X, Y, Z))

void SDL_GetVersion(SDL_version *ver);

/* ---- byte order ----------------------------------------------------- */

#define SDL_LIL_ENDIAN 1234
#define SDL_BIG_ENDIAN 4321
#define SDL_BYTEORDER SDL_BIG_ENDIAN

/* ---- init ----------------------------------------------------------- */

#define SDL_INIT_TIMER          0x00000001u
#define SDL_INIT_AUDIO          0x00000010u
#define SDL_INIT_VIDEO          0x00000020u
#define SDL_INIT_JOYSTICK       0x00000200u
#define SDL_INIT_HAPTIC         0x00001000u
#define SDL_INIT_GAMECONTROLLER 0x00002000u
#define SDL_INIT_EVENTS         0x00004000u
#define SDL_INIT_SENSOR         0x00008000u
#define SDL_INIT_EVERYTHING \
	(SDL_INIT_TIMER | SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_EVENTS | \
	 SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC | SDL_INIT_GAMECONTROLLER | SDL_INIT_SENSOR)

int SDL_Init(Uint32 flags);
Uint32 SDL_WasInit(Uint32 flags);
void SDL_QuitSubSystem(Uint32 flags);
void SDL_Quit(void);

const char *SDL_GetError(void);

#define SDL_HINT_JOYSTICK_HIDAPI_PS4_RUMBLE "SDL_JOYSTICK_HIDAPI_PS4_RUMBLE"
#define SDL_HINT_JOYSTICK_HIDAPI_PS5_RUMBLE "SDL_JOYSTICK_HIDAPI_PS5_RUMBLE"
#define SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS "SDL_GAMECONTROLLER_USE_BUTTON_LABELS"
SDL_bool SDL_SetHint(const char *name, const char *value);

Uint32 SDL_GetTicks(void);
void SDL_Delay(Uint32 ms);

/* ---- video (what the software renderer uses) ----------------------- */

typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Texture SDL_Texture;

typedef struct SDL_Rect
{
	int x, y;
	int w, h;
} SDL_Rect;

#define SDL_PIXELFORMAT_ARGB8888 0x16362004u
#define SDL_PIXELFORMAT_BGRA8888 0x16862004u
#define SDL_TEXTUREACCESS_STREAMING 1

#define SDL_RENDERER_SOFTWARE      0x00000001u
#define SDL_RENDERER_ACCELERATED   0x00000002u
#define SDL_RENDERER_PRESENTVSYNC  0x00000004u

#define SDL_WINDOW_FULLSCREEN         0x00000001u
#define SDL_WINDOW_FULLSCREEN_DESKTOP (SDL_WINDOW_FULLSCREEN | 0x00001000u)
#define SDL_WINDOW_ALLOW_HIGHDPI      0x00002000u

void SDL_SetWindowTitle(SDL_Window *window, const char *title);
Uint32 SDL_GetWindowFlags(SDL_Window *window);

SDL_Renderer *SDL_CreateRenderer(SDL_Window *window, int index, Uint32 flags);
void SDL_DestroyRenderer(SDL_Renderer *renderer);
int SDL_SetRenderDrawColor(SDL_Renderer *renderer, Uint8 r, Uint8 g, Uint8 b, Uint8 a);
int SDL_RenderClear(SDL_Renderer *renderer);
int SDL_RenderCopy(SDL_Renderer *renderer, SDL_Texture *texture,
		const SDL_Rect *srcrect, const SDL_Rect *dstrect);
void SDL_RenderPresent(SDL_Renderer *renderer);
int SDL_GetRendererOutputSize(SDL_Renderer *renderer, int *w, int *h);

SDL_Texture *SDL_CreateTexture(SDL_Renderer *renderer, Uint32 format,
		int access, int w, int h);
void SDL_DestroyTexture(SDL_Texture *texture);
int SDL_LockTexture(SDL_Texture *texture, const SDL_Rect *rect,
		void **pixels, int *pitch);
void SDL_UnlockTexture(SDL_Texture *texture);

/* ---- keyboard ------------------------------------------------------- */

#include "ps3_sdl_keys.h"

SDL_Keymod SDL_GetModState(void);
void SDL_StartTextInput(void);

char *SDL_GetClipboardText(void);
int SDL_SetClipboardText(const char *text);

/* ---- mouse ---------------------------------------------------------- */

#define SDL_BUTTON_LEFT   1
#define SDL_BUTTON_MIDDLE 2
#define SDL_BUTTON_RIGHT  3
#define SDL_BUTTON_X1     4
#define SDL_BUTTON_X2     5

/* ---- joystick / game controller ------------------------------------ */

typedef struct _SDL_Joystick SDL_Joystick;
typedef struct _SDL_GameController SDL_GameController;
typedef Sint32 SDL_JoystickID;

typedef struct
{
	Uint8 data[16];
} SDL_JoystickGUID;

typedef enum
{
	SDL_JOYSTICK_POWER_UNKNOWN = -1,
	SDL_JOYSTICK_POWER_EMPTY,
	SDL_JOYSTICK_POWER_LOW,
	SDL_JOYSTICK_POWER_MEDIUM,
	SDL_JOYSTICK_POWER_FULL,
	SDL_JOYSTICK_POWER_WIRED,
	SDL_JOYSTICK_POWER_MAX
} SDL_JoystickPowerLevel;

typedef enum
{
	SDL_CONTROLLER_TYPE_UNKNOWN = 0,
	SDL_CONTROLLER_TYPE_XBOX360,
	SDL_CONTROLLER_TYPE_XBOXONE,
	SDL_CONTROLLER_TYPE_PS3,
	SDL_CONTROLLER_TYPE_PS4,
	SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO,
	SDL_CONTROLLER_TYPE_VIRTUAL,
	SDL_CONTROLLER_TYPE_PS5,
	SDL_CONTROLLER_TYPE_AMAZON_LUNA,
	SDL_CONTROLLER_TYPE_GOOGLE_STADIA,
	SDL_CONTROLLER_TYPE_NVIDIA_SHIELD,
	SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_LEFT,
	SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT,
	SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR,
	SDL_CONTROLLER_TYPE_MAX
} SDL_GameControllerType;

/* The order matters: Yamagi maps button N to K_JOY_FIRST_BTN + N. */
typedef enum
{
	SDL_CONTROLLER_BUTTON_INVALID = -1,
	SDL_CONTROLLER_BUTTON_A,
	SDL_CONTROLLER_BUTTON_B,
	SDL_CONTROLLER_BUTTON_X,
	SDL_CONTROLLER_BUTTON_Y,
	SDL_CONTROLLER_BUTTON_BACK,
	SDL_CONTROLLER_BUTTON_GUIDE,
	SDL_CONTROLLER_BUTTON_START,
	SDL_CONTROLLER_BUTTON_LEFTSTICK,
	SDL_CONTROLLER_BUTTON_RIGHTSTICK,
	SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
	SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
	SDL_CONTROLLER_BUTTON_DPAD_UP,
	SDL_CONTROLLER_BUTTON_DPAD_DOWN,
	SDL_CONTROLLER_BUTTON_DPAD_LEFT,
	SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
	SDL_CONTROLLER_BUTTON_MISC1,
	SDL_CONTROLLER_BUTTON_PADDLE1,
	SDL_CONTROLLER_BUTTON_PADDLE2,
	SDL_CONTROLLER_BUTTON_PADDLE3,
	SDL_CONTROLLER_BUTTON_PADDLE4,
	SDL_CONTROLLER_BUTTON_TOUCHPAD,
	SDL_CONTROLLER_BUTTON_MAX
} SDL_GameControllerButton;

typedef enum
{
	SDL_CONTROLLER_AXIS_INVALID = -1,
	SDL_CONTROLLER_AXIS_LEFTX,
	SDL_CONTROLLER_AXIS_LEFTY,
	SDL_CONTROLLER_AXIS_RIGHTX,
	SDL_CONTROLLER_AXIS_RIGHTY,
	SDL_CONTROLLER_AXIS_TRIGGERLEFT,
	SDL_CONTROLLER_AXIS_TRIGGERRIGHT,
	SDL_CONTROLLER_AXIS_MAX
} SDL_GameControllerAxis;

typedef enum
{
	SDL_SENSOR_INVALID = -1,
	SDL_SENSOR_UNKNOWN,
	SDL_SENSOR_ACCEL,
	SDL_SENSOR_GYRO
} SDL_SensorType;

#define SDL_STANDARD_GRAVITY 9.80665f

int SDL_NumJoysticks(void);
SDL_Joystick *SDL_JoystickOpen(int device_index);
void SDL_JoystickClose(SDL_Joystick *joystick);
const char *SDL_JoystickName(SDL_Joystick *joystick);
int SDL_JoystickNumButtons(SDL_Joystick *joystick);
int SDL_JoystickNumAxes(SDL_Joystick *joystick);
int SDL_JoystickNumHats(SDL_Joystick *joystick);
SDL_JoystickID SDL_JoystickInstanceID(SDL_Joystick *joystick);
SDL_JoystickGUID SDL_JoystickGetDeviceGUID(int device_index);
void SDL_JoystickGetGUIDString(SDL_JoystickGUID guid, char *psz, int cb);

SDL_bool SDL_IsGameController(int joystick_index);
SDL_GameController *SDL_GameControllerOpen(int joystick_index);
void SDL_GameControllerClose(SDL_GameController *gamecontroller);
SDL_Joystick *SDL_GameControllerGetJoystick(SDL_GameController *gamecontroller);
SDL_GameControllerType SDL_GameControllerGetType(SDL_GameController *gamecontroller);
char *SDL_GameControllerMapping(SDL_GameController *gamecontroller);
int SDL_GameControllerAddMappingsFromFile(const char *file);
SDL_bool SDL_GameControllerHasSensor(SDL_GameController *gamecontroller, SDL_SensorType type);
int SDL_GameControllerSetSensorEnabled(SDL_GameController *gamecontroller,
		SDL_SensorType type, SDL_bool enabled);
float SDL_GameControllerGetSensorDataRate(SDL_GameController *gamecontroller, SDL_SensorType type);
SDL_bool SDL_GameControllerHasLED(SDL_GameController *gamecontroller);
int SDL_GameControllerSetLED(SDL_GameController *gamecontroller, Uint8 red, Uint8 green, Uint8 blue);
SDL_bool SDL_GameControllerHasRumble(SDL_GameController *gamecontroller);
int SDL_GameControllerRumble(SDL_GameController *gamecontroller,
		Uint16 low_frequency_rumble, Uint16 high_frequency_rumble, Uint32 duration_ms);

/* ---- haptic (never available: DS3 rumble goes through the controller) */

typedef struct _SDL_Haptic SDL_Haptic;

#define SDL_HAPTIC_SINE      (1u << 1)
#define SDL_HAPTIC_CARTESIAN 1

typedef struct SDL_HapticDirection
{
	Uint8 type;
	Sint32 dir[3];
} SDL_HapticDirection;

typedef struct SDL_HapticPeriodic
{
	Uint16 type;
	SDL_HapticDirection direction;
	Uint32 length;
	Uint16 delay;
	Uint16 button;
	Uint16 interval;
	Uint16 period;
	Sint16 magnitude;
	Sint16 offset;
	Uint16 phase;
	Uint16 attack_length;
	Uint16 attack_level;
	Uint16 fade_length;
	Uint16 fade_level;
} SDL_HapticPeriodic;

typedef union SDL_HapticEffect
{
	Uint16 type;
	SDL_HapticPeriodic periodic;
} SDL_HapticEffect;

SDL_Haptic *SDL_HapticOpenFromJoystick(SDL_Joystick *joystick);
SDL_Haptic *SDL_HapticOpenFromMouse(void);
void SDL_HapticClose(SDL_Haptic *haptic);
unsigned int SDL_HapticQuery(SDL_Haptic *haptic);
int SDL_HapticNumEffects(SDL_Haptic *haptic);
int SDL_HapticNumEffectsPlaying(SDL_Haptic *haptic);
int SDL_HapticNumAxes(SDL_Haptic *haptic);
int SDL_HapticNewEffect(SDL_Haptic *haptic, SDL_HapticEffect *effect);
int SDL_HapticRunEffect(SDL_Haptic *haptic, int effect, Uint32 iterations);
void SDL_HapticDestroyEffect(SDL_Haptic *haptic, int effect);

/* ---- events --------------------------------------------------------- */

typedef enum
{
	SDL_FIRSTEVENT = 0,
	SDL_QUIT = 0x100,
	SDL_WINDOWEVENT = 0x200,
	SDL_KEYDOWN = 0x300,
	SDL_KEYUP,
	SDL_TEXTINPUT = 0x303,
	SDL_MOUSEMOTION = 0x400,
	SDL_MOUSEBUTTONDOWN,
	SDL_MOUSEBUTTONUP,
	SDL_MOUSEWHEEL,
	SDL_JOYAXISMOTION = 0x600,
	SDL_JOYDEVICEADDED = 0x605,
	SDL_JOYBATTERYUPDATED = 0x607,
	SDL_CONTROLLERAXISMOTION = 0x650,
	SDL_CONTROLLERBUTTONDOWN,
	SDL_CONTROLLERBUTTONUP,
	SDL_CONTROLLERDEVICEADDED,
	SDL_CONTROLLERDEVICEREMOVED,
	SDL_CONTROLLERSENSORUPDATE = 0x659,
	SDL_LASTEVENT = 0xFFFF
} SDL_EventType;

typedef enum
{
	SDL_WINDOWEVENT_NONE,
	SDL_WINDOWEVENT_SHOWN,
	SDL_WINDOWEVENT_HIDDEN,
	SDL_WINDOWEVENT_EXPOSED,
	SDL_WINDOWEVENT_MOVED,
	SDL_WINDOWEVENT_RESIZED,
	SDL_WINDOWEVENT_SIZE_CHANGED,
	SDL_WINDOWEVENT_MINIMIZED,
	SDL_WINDOWEVENT_MAXIMIZED,
	SDL_WINDOWEVENT_RESTORED,
	SDL_WINDOWEVENT_ENTER,
	SDL_WINDOWEVENT_LEAVE,
	SDL_WINDOWEVENT_FOCUS_GAINED,
	SDL_WINDOWEVENT_FOCUS_LOST
} SDL_WindowEventID;

typedef struct SDL_Keysym
{
	SDL_Scancode scancode;
	SDL_Keycode sym;
	Uint16 mod;
} SDL_Keysym;

typedef struct { Uint32 type; Uint32 timestamp; Uint8 event; } SDL_WindowEvent;
typedef struct { Uint32 type; Uint32 timestamp; Uint8 state; SDL_Keysym keysym; } SDL_KeyboardEvent;
typedef struct { Uint32 type; Uint32 timestamp; char text[32]; } SDL_TextInputEvent;
typedef struct { Uint32 type; Uint32 timestamp; Sint32 x, y, xrel, yrel; } SDL_MouseMotionEvent;
typedef struct { Uint32 type; Uint32 timestamp; Uint8 button; Uint8 state; } SDL_MouseButtonEvent;
typedef struct { Uint32 type; Uint32 timestamp; Sint32 x, y; } SDL_MouseWheelEvent;
typedef struct { Uint32 type; Uint32 timestamp; SDL_JoystickID which; Uint8 axis; Sint16 value; } SDL_ControllerAxisEvent;
typedef struct { Uint32 type; Uint32 timestamp; SDL_JoystickID which; Uint8 button; Uint8 state; } SDL_ControllerButtonEvent;
typedef struct { Uint32 type; Uint32 timestamp; Sint32 which; } SDL_ControllerDeviceEvent;
typedef struct { Uint32 type; Uint32 timestamp; SDL_JoystickID which; Sint32 sensor; float data[3]; } SDL_ControllerSensorEvent;
typedef struct { Uint32 type; Uint32 timestamp; SDL_JoystickID which; SDL_JoystickPowerLevel level; } SDL_JoyBatteryEvent;
typedef struct { Uint32 type; Uint32 timestamp; Sint32 which; } SDL_JoyDeviceEvent;

typedef union SDL_Event
{
	Uint32 type;
	SDL_WindowEvent window;
	SDL_KeyboardEvent key;
	SDL_TextInputEvent text;
	SDL_MouseMotionEvent motion;
	SDL_MouseButtonEvent button;
	SDL_MouseWheelEvent wheel;
	SDL_ControllerAxisEvent caxis;
	SDL_ControllerButtonEvent cbutton;
	SDL_ControllerDeviceEvent cdevice;
	SDL_ControllerSensorEvent csensor;
	SDL_JoyBatteryEvent jbattery;
	SDL_JoyDeviceEvent jdevice;
	Uint8 padding[56];
} SDL_Event;

int SDL_PollEvent(SDL_Event *event);
void SDL_FlushEvents(Uint32 minType, Uint32 maxType);

/* ---- audio (see src/client/sound/sdl.c) ---------------------------- */

typedef Uint16 SDL_AudioFormat;

#define AUDIO_U8     0x0008
#define AUDIO_S8     0x8008
#define AUDIO_S16LSB 0x8010
#define AUDIO_S16MSB 0x9010
#define AUDIO_S16SYS AUDIO_S16MSB
#define AUDIO_S16    AUDIO_S16LSB

typedef void (*SDL_AudioCallback)(void *userdata, Uint8 *stream, int len);

typedef struct SDL_AudioSpec
{
	int freq;
	SDL_AudioFormat format;
	Uint8 channels;
	Uint8 silence;
	Uint16 samples;
	Uint16 padding;
	Uint32 size;
	SDL_AudioCallback callback;
	void *userdata;
} SDL_AudioSpec;

const char *SDL_GetCurrentAudioDriver(void);
int SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained);
void SDL_PauseAudio(int pause_on);
void SDL_LockAudio(void);
void SDL_UnlockAudio(void);
void SDL_CloseAudio(void);

#endif
