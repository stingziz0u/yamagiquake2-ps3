# Source lists, shared by ps3/Makefile and ps3/hosttest/Makefile.
# Needs $(SRC) (tree root), $(OBJDIR) and $(STAGE).
#
# Paths are relative to the tree root. Upstream files are used as they
# are; the PS3 specific ones live in src/backends/ps3/.

#----------------------------------------------------------------------------
# Platform layer
#----------------------------------------------------------------------------
PS3_SRCS := \
	src/backends/ps3/main.c \
	src/backends/ps3/system.c \
	src/backends/ps3/hunk.c \
	src/backends/ps3/network.c \
	src/backends/ps3/ps3_log.c

# Client side: display, pad, and the small SDL the Yamagi files expect.
# videomenu_ps3.c replaces src/client/menu/videomenu.c (PC options).
# gl/ is ps3gl, the OpenGL 1.x ref_gl1 draws with (on the RSX): it's
# the engine's "libGL", ref_gl1 calls it like it would call the system's.
PS3_CLIENT_SRCS := \
	src/backends/ps3/glimp_ps3.c \
	src/backends/ps3/videomenu_ps3.c \
	src/backends/ps3/sdl_ps3.c \
	src/backends/ps3/ps3_video.c \
	src/backends/ps3/ps3_pad.c \
	src/backends/ps3/gl/ps3gl_main.c \
	src/backends/ps3/gl/ps3gl_state.c \
	src/backends/ps3/gl/ps3gl_texture.c \
	src/backends/ps3/gl/ps3gl_draw.c

#----------------------------------------------------------------------------
# Engine
#----------------------------------------------------------------------------
COMMON_SRCS := \
	src/common/argproc.c \
	src/common/clientserver.c \
	src/common/collision.c \
	src/common/crc.c \
	src/common/cmdparser.c \
	src/common/cvar.c \
	src/common/filesystem.c \
	src/common/glob.c \
	src/common/md4.c \
	src/common/frame.c \
	src/common/movemsg.c \
	src/common/netchan.c \
	src/common/pmove.c \
	src/common/szone.c \
	src/common/zone.c \
	src/common/shared/rand.c \
	src/common/shared/shared.c \
	src/common/unzip/ioapi.c \
	src/common/unzip/unzip.c \
	src/common/unzip/miniz/miniz.c \
	src/common/unzip/miniz/miniz_tdef.c \
	src/common/unzip/miniz/miniz_tinfl.c

SERVER_SRCS := \
	src/server/sv_cmd.c \
	src/server/sv_conless.c \
	src/server/sv_entities.c \
	src/server/sv_game.c \
	src/server/sv_init.c \
	src/server/sv_main.c \
	src/server/sv_save.c \
	src/server/sv_send.c \
	src/server/sv_user.c \
	src/server/sv_world.c

# Yamagi's client minus curl, OpenAL and glimp_sdl2.c (-> glimp_ps3.c).
# input/sdl2.c and sound/sdl.c build unchanged against the SDL shim.
CLIENT_SRCS := \
	src/common/shared/flash.c \
	src/client/cl_cin.c \
	src/client/cl_image.c \
	src/client/cl_console.c \
	src/client/cl_download.c \
	src/client/cl_effects.c \
	src/client/cl_entities.c \
	src/client/cl_input.c \
	src/client/cl_inventory.c \
	src/client/cl_keyboard.c \
	src/client/cl_lights.c \
	src/client/cl_main.c \
	src/client/cl_network.c \
	src/client/cl_parse.c \
	src/client/cl_particles.c \
	src/client/cl_prediction.c \
	src/client/cl_screen.c \
	src/client/cl_tempentities.c \
	src/client/cl_view.c \
	src/client/input/gyro.c \
	src/client/input/sdl2.c \
	src/client/menu/menu.c \
	src/client/menu/qmenu.c \
	src/client/sound/ogg.c \
	src/client/sound/sdl.c \
	src/client/sound/sound.c \
	src/client/sound/wave.c \
	src/client/vid/vid.c

#----------------------------------------------------------------------------
# The game "DLL" (baseq2). Linked into one object where only GetGameAPI
# stays global, so its private copy of shared.c and its globals can't
# collide with the engine's -- exactly like a real DLL. Mission packs will
# be added the same way, with GetGameAPI renamed.
#----------------------------------------------------------------------------
GAME_SRCS := \
	src/common/shared/flash.c \
	src/common/shared/rand.c \
	src/common/shared/shared.c \
	$(sort $(wildcard $(SRC)/src/game/*.c $(SRC)/src/game/*/*.c $(SRC)/src/game/*/*/*.c))
GAME_SRCS := $(patsubst $(SRC)/%,%,$(GAME_SRCS))

#----------------------------------------------------------------------------
# The software renderer "DLL" (ref_soft), same treatment: only GetRefAPI
# stays global, renamed to GetRefAPI_soft (src/client/vid/vid.c).
#----------------------------------------------------------------------------
REFSOFT_SRCS := \
	$(patsubst $(SRC)/%,%,$(sort $(wildcard $(SRC)/src/client/refresh/soft/*.c))) \
	src/client/refresh/files/surf.c \
	src/client/refresh/files/common.c \
	src/client/refresh/files/models.c \
	src/client/refresh/files/pcx.c \
	src/client/refresh/files/stb.c \
	src/client/refresh/files/wal.c \
	src/client/refresh/files/pvs.c \
	src/common/shared/shared.c \
	src/common/md4.c \
	src/backends/ps3/hunk.c

#----------------------------------------------------------------------------
# The OpenGL renderer "DLL" (ref_gl1), same treatment, GetRefAPI renamed
# to GetRefAPI_gl1. gl1_sdl.c (SDL's GL context) is replaced by
# src/backends/ps3/gl1_ps3.c.
#----------------------------------------------------------------------------
REFGL1_SRCS := \
	src/client/refresh/gl1/qgl.c \
	src/client/refresh/gl1/gl1_draw.c \
	src/client/refresh/gl1/gl1_image.c \
	src/client/refresh/gl1/gl1_light.c \
	src/client/refresh/gl1/gl1_lightmap.c \
	src/client/refresh/gl1/gl1_main.c \
	src/client/refresh/gl1/gl1_mesh.c \
	src/client/refresh/gl1/gl1_misc.c \
	src/client/refresh/gl1/gl1_model.c \
	src/client/refresh/gl1/gl1_scrap.c \
	src/client/refresh/gl1/gl1_surf.c \
	src/client/refresh/gl1/gl1_warp.c \
	src/client/refresh/gl1/gl1_buffer.c \
	src/backends/ps3/gl1_ps3.c \
	src/client/refresh/files/common.c \
	src/client/refresh/files/surf.c \
	src/client/refresh/files/models.c \
	src/client/refresh/files/pcx.c \
	src/client/refresh/files/stb.c \
	src/client/refresh/files/wal.c \
	src/client/refresh/files/pvs.c \
	src/common/shared/shared.c \
	src/common/md4.c \
	src/backends/ps3/hunk.c

#----------------------------------------------------------------------------
# The mission packs and Zaero, from Yamagi's own repositories (fetched
# into ps3/addons/ by 'make -C ps3 addons', plus the fixes in
# ps3/addon-fixes/). Each is linked into one object like baseq2, with
# GetGameAPI renamed to GetGameAPI_xatrix / _rogue / _zaero
# (src/backends/ps3/system.c picks one).
#----------------------------------------------------------------------------
addon_srcs = $(patsubst $(SRC)/%,%,$(sort $(wildcard $(SRC)/ps3/addons/$(1)/src/*.c \
	$(SRC)/ps3/addons/$(1)/src/*/*.c $(SRC)/ps3/addons/$(1)/src/*/*/*.c)))

XATRIX_SRCS := $(call addon_srcs,xatrix)
ROGUE_SRCS  := $(call addon_srcs,rogue)
ZAERO_SRCS  := $(call addon_srcs,zaero)

#----------------------------------------------------------------------------
# What goes in, per stage
#----------------------------------------------------------------------------
ifeq ($(STAGE),1)
ENGINE_SRCS := $(PS3_SRCS) $(COMMON_SRCS) $(SERVER_SRCS)
REF_O :=
else
ENGINE_SRCS := $(PS3_SRCS) $(PS3_CLIENT_SRCS) $(COMMON_SRCS) $(SERVER_SRCS) $(CLIENT_SRCS)
REF_O := $(OBJDIR)/ref_soft.o $(OBJDIR)/ref_gl1.o
ADDON_O := $(OBJDIR)/game_xatrix.o $(OBJDIR)/game_rogue.o
ifeq ($(ZAERO),1)
ADDON_O += $(OBJDIR)/game_zaero.o
endif
endif

ENGINE_OBJS := $(patsubst %.c,$(OBJDIR)/engine/%.o,$(ENGINE_SRCS))
GAME_OBJS   := $(patsubst %.c,$(OBJDIR)/game/%.o,$(GAME_SRCS))
REF_OBJS    := $(patsubst %.c,$(OBJDIR)/ref/%.o,$(REFSOFT_SRCS))
REFGL1_OBJS := $(patsubst %.c,$(OBJDIR)/refgl1/%.o,$(REFGL1_SRCS))
XATRIX_OBJS := $(patsubst %.c,$(OBJDIR)/xatrix/%.o,$(XATRIX_SRCS))
ROGUE_OBJS  := $(patsubst %.c,$(OBJDIR)/rogue/%.o,$(ROGUE_SRCS))
ZAERO_OBJS  := $(patsubst %.c,$(OBJDIR)/zaero/%.o,$(ZAERO_SRCS))
GAME_O      := $(OBJDIR)/game.o
