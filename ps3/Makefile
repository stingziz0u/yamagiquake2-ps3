#----------------------------------------------------------------------------
# Quake2PS3 -- Yamagi Quake II on the PS3 (PSL1GHT)
#
# From the root of the tree:
#
#   make -C ps3 addons         (once: the mission packs' sources)
#   make -C ps3                -> ps3/quake2ps3-1.1.pkg
#   make -C ps3 ZAERO=1        -> the same with Zaero linked in, for
#                                 personal use only (see below)
#   make -C ps3 STAGE=1        -> ps3/quake2ps3-1.1-stage1.pkg (test)
#   make -C ps3 clean
#   make -C ps3 check-toolchain
#
# Stage 1: headless. common + server + game, no client, no video, no audio.
# The PKG loads a map, changes level, and quits by itself (~20 s of black
# screen). The result is in USRDIR/quake2_log.txt.
#
# Stage 2 needs the mission packs' sources once (git, internet):
#
#   make -C ps3 addons         -> ps3/addons/xatrix, rogue
#   make -C ps3 addons ZAERO=1 -> ... and zaero
#
# ZAERO: Zaero's game code is NOT under the GPL but under id Software's
# Limited Program Source Code License (the Quake II SDK license), which
# can't be combined with GPL code in one program. So it is off by default
# and never part of a published PKG: ZAERO=1 is for building your own.
#
# Stage 2: the client. Two renderers: OpenGL (ref_gl1 over ps3gl, drawn
# by the RSX, the default) and software (CPU). Both scaled to the TV by
# the RSX. DualShock 3. Sound and Ogg music through the audio port.
#
# Each stage builds in its own object dir, switching needs no clean.
#
# Packaging (.elf -> .self -> .pkg) is done by ppu_rules' own pattern rules
# (strip + sprxlinker + sign + package_finalize). Never by hand.
#----------------------------------------------------------------------------

ifeq ($(strip $(PS3DEV)),)
$(error PS3DEV is not set. Do: export PS3DEV=/usr/local/ps3dev)
endif

include $(PS3DEV)/ppu_rules

STAGE     ?= 2
ZAERO     ?= 0

#----------------------------------------------------------------------------
# Package identity
#
# APPID: EXACTLY 9 characters (with 8 the PS3 pads it with "_" and every
# path built from it stops matching). ':=' and not '?=': ppu_rules sets
# TITLE/APPID with '?=' BEFORE these lines.
#----------------------------------------------------------------------------
# The intermediate files (.elf, .self, .pkg) are named per variant, so a
# build of one never picks up another's leftovers.
ifeq ($(STAGE),1)
TARGET    := quake2ps3-stage1
else ifeq ($(ZAERO),1)
TARGET    := quake2ps3-zaero
else
TARGET    := quake2ps3
endif
TITLE     := Quake II
APPID     := QUAKE2PS3
CONTENTID := UP0001-$(APPID)_00-0000000000000000

PKGFILES  := $(CURDIR)/pkgfiles
ICON0     := $(CURDIR)/ICON0.PNG

ifeq ($(STAGE),1)
VERSION   := 1.1-stage1
else ifeq ($(ZAERO),1)
VERSION   := 1.1-zaero
else
VERSION   := 1.1
endif
OUT_PKG   := quake2ps3-$(VERSION).pkg

SRC       := $(abspath $(CURDIR)/..)
# ZAERO=1 changes the flags of some files: its own object dir, so
# switching needs no clean.
ifeq ($(ZAERO),1)
OBJDIR    := $(CURDIR)/obj-stage$(STAGE)-zaero
else
OBJDIR    := $(CURDIR)/obj-stage$(STAGE)
endif

CC        := ppu-gcc
LD        := ppu-gcc
PARTIALLD := ppu-ld
OBJCOPY   := ppu-objcopy

#----------------------------------------------------------------------------
# Flags
#----------------------------------------------------------------------------
# Upstream's own: -fno-strict-aliasing -fwrapv -fvisibility=hidden, and
# -fno-common so two globals with the same name are a link error instead of
# silently becoming one variable (the game used to be a separate DLL, now
# it is linked in).
CFLAGS := -O2 -std=gnu11 -Wall -fno-strict-aliasing -fwrapv -fvisibility=hidden -fno-common
CFLAGS += -Wno-unused-variable -Wno-unused-function -Wno-unused-but-set-variable
# PPU is LP64: a missing prototype truncates pointers and 64-bit values to
# int. Make those hard errors (handoff notes 3.26).
CFLAGS += -Werror=implicit-function-declaration -Werror=int-conversion
CFLAGS += -Werror=pointer-to-int-cast -Werror=int-to-pointer-cast
CFLAGS += -MMD -MP
# compat/ first: <SDL2/SDL.h> must find the PS3 shim (compat/SDL2/SDL.h).
CFLAGS += -I$(SRC)/src/backends/ps3/compat
CFLAGS += -D__PS3__ -DPS3_APPID=\"$(APPID)\" -DYQ2OSTYPE=\"PS3\" -DYQ2ARCH=\"ppc64\"
CFLAGS += -DBUILD_DATE=\"$(shell date +%Y-%m-%d)\"
CFLAGS += -DNOUNCRYPT -DIOAPI_NO_64
CFLAGS += $(LIBPSL1GHT_INC)

ifeq ($(STAGE),1)
CFLAGS += -DDEDICATED_ONLY -DPS3_STAGE1
else
CFLAGS += -DPS3_ADDONS
ifeq ($(ZAERO),1)
CFLAGS += -DPS3_ZAERO
endif
endif

#----------------------------------------------------------------------------
# Mission packs: Yamagi's repositories at a fixed commit
#----------------------------------------------------------------------------
XATRIX_REPO   := https://github.com/yquake2/xatrix.git
XATRIX_COMMIT := 4ef7e671acd217b9cb670b54c1fc42e3da9a2345
ROGUE_REPO    := https://github.com/yquake2/rogue.git
ROGUE_COMMIT  := 6a9ac36df35fb57da70f9268e3bac8ad07cf8dca
ZAERO_REPO    := https://github.com/yquake2/zaero.git
ZAERO_COMMIT  := f78d7e07f97a11094115ce51c52c591df4e5514a

ifeq ($(STAGE),2)
ifeq ($(filter addons clean check-toolchain,$(MAKECMDGOALS)),)
ifeq ($(wildcard $(CURDIR)/addons/xatrix/src/g_main.c),)
$(error The mission packs' sources are missing: run 'make -C ps3 addons' first)
endif
ifeq ($(wildcard $(CURDIR)/addons/rogue/src/g_main.c),)
$(error The mission packs' sources are missing: run 'make -C ps3 addons' first)
endif
ifeq ($(ZAERO),1)
ifeq ($(wildcard $(CURDIR)/addons/zaero/src/g_main.c),)
$(error Zaero's sources are missing: run 'make -C ps3 addons ZAERO=1' first)
endif
endif
endif
endif

# -lrt and -llv2 are MANDATORY: without them the binary links cleanly, the
# PKG installs, and it never reaches main(). Black screen, back to the XMB,
# no log at all.
LIBS := gcm_sys rsx sysutil io audio rt lv2 m

include $(CURDIR)/sources.mk

# The file that talks to GCM goes at -O1 -fno-inline: PSL1GHT's syscall
# stubs are data indirection symbols and aggressive inlining breaks their
# resolution at link time (handoff notes 4.2).
CFLAGS_VIDEO := $(filter-out -O2,$(CFLAGS)) -O1 -fno-inline
$(OBJDIR)/engine/src/backends/ps3/ps3_video.o: CFLAGS := $(CFLAGS_VIDEO)

# Optimization of the software renderer, for A/B tests (compare the fps
# lines in the log). Always with a clean build, make doesn't track flags:
#   make -C ps3 clean && make -C ps3 REFOPT="-O3 -funroll-loops"
# (Tried: -O3 -funroll-loops gives the same frame times as -O2.)
#
# The renderer's own stage timers (r_dspeeds) call SDL_GetTicks, which is
# renamed for the renderer object to PS3_ProfileTicks, a microsecond clock
# (ps3_video.c). The results go to the log every 10 seconds.
REFOPT ?= -O2
CFLAGS_REF := $(filter-out -O2,$(CFLAGS)) $(REFOPT)

# Mission packs: built as they come, with fopen()/stat() routed through
# the PS3's path resolution (see src/backends/ps3/ps3_addon.h).
CFLAGS_ADDON := $(CFLAGS) -include $(SRC)/src/backends/ps3/ps3_addon.h

# ref_gl1: no hardware gamma ramp on the PS3, gamma goes into the
# textures when they're loaded.
CFLAGS_REFGL1 := $(CFLAGS) -DGL1_GAMMATABLE

#----------------------------------------------------------------------------
# Rules
#----------------------------------------------------------------------------
.PHONY: all clean check-toolchain addons

all: $(TARGET).pkg
	@cp $(TARGET).gnpdrm.pkg $(OUT_PKG)
	@echo ""
	@echo "PKG to install: $(CURDIR)/$(OUT_PKG)"
	@ls -la $(OUT_PKG)

$(OBJDIR)/engine/%.o: $(SRC)/%.c
	@mkdir -p $(dir $@)
	@echo "CC  $*.c"
	@$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/game/%.o: $(SRC)/%.c
	@mkdir -p $(dir $@)
	@echo "CC  [game] $*.c"
	@$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/ref/%.o: $(SRC)/%.c
	@mkdir -p $(dir $@)
	@echo "CC  [ref_soft] $*.c"
	@$(CC) $(CFLAGS_REF) -c $< -o $@

$(OBJDIR)/refgl1/%.o: $(SRC)/%.c
	@mkdir -p $(dir $@)
	@echo "CC  [ref_gl1] $*.c"
	@$(CC) $(CFLAGS_REFGL1) -c $< -o $@

$(OBJDIR)/xatrix/%.o: $(SRC)/%.c
	@mkdir -p $(dir $@)
	@echo "CC  [xatrix] $(notdir $*).c"
	@$(CC) $(CFLAGS_ADDON) -c $< -o $@

$(OBJDIR)/rogue/%.o: $(SRC)/%.c
	@mkdir -p $(dir $@)
	@echo "CC  [rogue] $(notdir $*).c"
	@$(CC) $(CFLAGS_ADDON) -c $< -o $@

$(OBJDIR)/zaero/%.o: $(SRC)/%.c
	@mkdir -p $(dir $@)
	@echo "CC  [zaero] $(notdir $*).c"
	@$(CC) $(CFLAGS_ADDON) -c $< -o $@

$(OBJDIR)/game_zaero.o: $(ZAERO_OBJS)
	@echo "LD  game_zaero.o (only GetGameAPI_zaero stays global)"
	@$(PARTIALLD) -r -o $(OBJDIR)/zaero_all.o $(ZAERO_OBJS)
	@$(OBJCOPY) --redefine-sym GetGameAPI=GetGameAPI_zaero $(OBJDIR)/zaero_all.o $(OBJDIR)/zaero_renamed.o
	@$(OBJCOPY) --keep-global-symbol=GetGameAPI_zaero $(OBJDIR)/zaero_renamed.o $@

$(OBJDIR)/game_xatrix.o: $(XATRIX_OBJS)
	@echo "LD  game_xatrix.o (only GetGameAPI_xatrix stays global)"
	@$(PARTIALLD) -r -o $(OBJDIR)/xatrix_all.o $(XATRIX_OBJS)
	@$(OBJCOPY) --redefine-sym GetGameAPI=GetGameAPI_xatrix $(OBJDIR)/xatrix_all.o $(OBJDIR)/xatrix_renamed.o
	@$(OBJCOPY) --keep-global-symbol=GetGameAPI_xatrix $(OBJDIR)/xatrix_renamed.o $@

$(OBJDIR)/game_rogue.o: $(ROGUE_OBJS)
	@echo "LD  game_rogue.o (only GetGameAPI_rogue stays global)"
	@$(PARTIALLD) -r -o $(OBJDIR)/rogue_all.o $(ROGUE_OBJS)
	@$(OBJCOPY) --redefine-sym GetGameAPI=GetGameAPI_rogue $(OBJDIR)/rogue_all.o $(OBJDIR)/rogue_renamed.o
	@$(OBJCOPY) --keep-global-symbol=GetGameAPI_rogue $(OBJDIR)/rogue_renamed.o $@

$(GAME_O): $(GAME_OBJS)
	@echo "LD  game.o (only GetGameAPI stays global)"
	@$(PARTIALLD) -r -o $(OBJDIR)/game_all.o $(GAME_OBJS)
	@$(OBJCOPY) --keep-global-symbol=GetGameAPI $(OBJDIR)/game_all.o $@

$(OBJDIR)/ref_soft.o: $(REF_OBJS)
	@echo "LD  ref_soft.o (only GetRefAPI_soft stays global)"
	@$(PARTIALLD) -r -o $(OBJDIR)/ref_all.o $(REF_OBJS)
	@$(OBJCOPY) --redefine-sym GetRefAPI=GetRefAPI_soft --redefine-sym SDL_GetTicks=PS3_ProfileTicks \
		$(OBJDIR)/ref_all.o $(OBJDIR)/ref_renamed.o
	@$(OBJCOPY) --keep-global-symbol=GetRefAPI_soft $(OBJDIR)/ref_renamed.o $@

$(OBJDIR)/ref_gl1.o: $(REFGL1_OBJS)
	@echo "LD  ref_gl1.o (only GetRefAPI_gl1 stays global)"
	@$(PARTIALLD) -r -o $(OBJDIR)/refgl1_all.o $(REFGL1_OBJS)
	@$(OBJCOPY) --redefine-sym GetRefAPI=GetRefAPI_gl1 $(OBJDIR)/refgl1_all.o $(OBJDIR)/refgl1_renamed.o
	@$(OBJCOPY) --keep-global-symbol=GetRefAPI_gl1 $(OBJDIR)/refgl1_renamed.o $@

$(TARGET).elf: $(ENGINE_OBJS) $(GAME_O) $(ADDON_O) $(REF_O)
	@echo "LD  $@"
	@$(LD) $(ENGINE_OBJS) $(GAME_O) $(ADDON_O) $(REF_O) $(LIBPSL1GHT_LIB) -o $@ $(foreach lib,$(LIBS),-l$(lib))

-include $(ENGINE_OBJS:.o=.d) $(GAME_OBJS:.o=.d) $(REF_OBJS:.o=.d) $(REFGL1_OBJS:.o=.d)
-include $(XATRIX_OBJS:.o=.d) $(ROGUE_OBJS:.o=.d) $(ZAERO_OBJS:.o=.d)

#----------------------------------------------------------------------------
# PKG contents and icon
#
# pkgfiles/ mirrors the PKG, not its USRDIR (ppu_rules does
# 'cp -rf $(PKGFILES)/* $(BUILDDIR)/pkg/'). VERSION.TXT is always written
# -- it also tells which build is installed. Game data is NOT packaged:
# it goes by FTP into the folders below.
#----------------------------------------------------------------------------
# Rewritten when the variant or the day changes (so a PKG always says
# what it is), left alone otherwise (so the PKG isn't rebuilt for nothing).
.PHONY: FORCE
$(PKGFILES)/USRDIR/VERSION.TXT: FORCE
	@mkdir -p $(PKGFILES)/USRDIR
	@echo "$(TITLE) -- Quake2PS3 $(VERSION) -- `date +%Y-%m-%d`" > $@.tmp
	@if cmp -s $@.tmp $@; then rm -f $@.tmp; else mv -f $@.tmp $@; fi

$(ICON0):
	@echo "No ICON0.PNG, generating a 320x176 placeholder..."
	@python3 -c "import zlib,struct;w,h=320,176;\
rows=b''.join(b'\x00'+bytes([60,40,20,255])*w for _ in range(h));\
png=lambda t,d:struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d));\
open('$(ICON0)','wb').write(b'\x89PNG\r\n\x1a\n'+png(b'IHDR',struct.pack('>IIBBBBB',w,h,8,6,0,0,0))+png(b'IDAT',zlib.compress(rows))+png(b'IEND',b''))"

# The folders the user fills (game data, videos, music, mods). Created
# at build time: they are empty, and neither git nor a patch keeps empty
# folders. pkg.py packs empty folders too, so the PS3 creates them.
PKG_DIRS := USRDIR/baseq2/video USRDIR/music \
	USRDIR/xatrix/video USRDIR/rogue/video USRDIR/mods
ifeq ($(ZAERO),1)
PKG_DIRS += USRDIR/mods/zaero/video
endif

.PHONY: pkgdirs
pkgdirs:
	@for d in $(PKG_DIRS); do mkdir -p "$(PKGFILES)/$$d"; done
ifneq ($(ZAERO),1)
	@rmdir "$(PKGFILES)/USRDIR/mods/zaero/video" "$(PKGFILES)/USRDIR/mods/zaero" 2>/dev/null || true
endif

$(TARGET).pkg: $(PKGFILES)/USRDIR/VERSION.TXT $(ICON0) | pkgdirs

#----------------------------------------------------------------------------
# Fetches (or moves to the pinned commit) the mission packs' sources.
#----------------------------------------------------------------------------
ADDON_LIST := "xatrix $(XATRIX_REPO) $(XATRIX_COMMIT)" "rogue $(ROGUE_REPO) $(ROGUE_COMMIT)"
ifeq ($(ZAERO),1)
ADDON_LIST += "zaero $(ZAERO_REPO) $(ZAERO_COMMIT)"
endif

addons:
	@for a in $(ADDON_LIST); do \
	   set -- $$a; \
	   d="$(CURDIR)/addons/$$1"; \
	   if [ ! -d "$$d/.git" ]; then \
	     echo "cloning $$2"; \
	     git clone -q "$$2" "$$d" || exit 1; \
	   fi; \
	   git -C "$$d" checkout -q -- . ; \
	   if ! git -C "$$d" checkout -q "$$3" 2>/dev/null; then \
	     git -C "$$d" fetch -q origin && git -C "$$d" checkout -q "$$3" || exit 1; \
	   fi; \
	   for f in $(CURDIR)/addon-fixes/$$1-*.patch; do \
	     [ -f "$$f" ] || continue; \
	     git -C "$$d" apply "$$f" || exit 1; \
	     echo "  fix: `basename $$f`"; \
	   done; \
	   echo "addons/$$1 at $$3"; \
	 done

#----------------------------------------------------------------------------
# Diagnostics
#----------------------------------------------------------------------------
check-toolchain:
	@echo "PS3DEV   = $(PS3DEV)"
	@echo "ppu_rules: `test -f $(PS3DEV)/ppu_rules && echo OK || echo MISSING`"
	@$(CC) --version | head -1
	@echo "APPID    = $(APPID)  (length: `printf '%s' '$(APPID)' | wc -c`, MUST be 9)"
	@echo "TITLE    = $(TITLE)"
	@for l in $(LIBS); do \
	   if [ -f "$(PS3DEV)/ppu/lib/lib$$l.a" ] || [ -f "$(PSL1GHT)/ppu/lib/lib$$l.a" ]; then \
	     printf "  OK      lib%s.a\n" "$$l"; \
	   elif $(CC) -print-file-name=lib$$l.a | grep -q /; then \
	     printf "  OK      lib%s.a (gcc sysroot)\n" "$$l"; \
	   else \
	     printf "  MISSING lib%s.a\n" "$$l"; \
	   fi; \
	 done

clean:
	rm -rf $(CURDIR)/obj-stage1 $(CURDIR)/obj-stage2 $(CURDIR)/obj-stage2-zaero build
	rm -f quake2ps3*.elf quake2ps3*.self
	rm -f $(PKGFILES)/USRDIR/VERSION.TXT
	@echo "clean (the .pkg is left alone: ppu_rules rebuilds it)"
