#----------------------------------------------------------------------------
# Host harness for Quake2PS3: the same sources as the PKG (ps3/sources.mk),
# built for powerpc64 big-endian Linux and run under qemu-ppc64. It is not
# the PS3 (no RSX, no LV2, glibc instead of newlib), but it is the same
# CPU family, byte order, char signedness and LP64 model, so data loading,
# game logic and the save code can be tested without a trip to the console.
#
# Needs (Ubuntu): gcc-powerpc64-linux-gnu qemu-user
#
#   make -C ps3/hosttest run DATA=/path/with/baseq2
#
# DATA must contain baseq2/pak0.pak. It is used as USRDIR; home (config,
# saves) goes to ps3/hosttest/home.
#----------------------------------------------------------------------------

SRC      := $(abspath $(CURDIR)/../..)
OBJDIR   := $(CURDIR)/obj
DATA     ?= $(CURDIR)/data
HOME_DIR := $(CURDIR)/home
STAGE    := 1

CROSS     := powerpc64-linux-gnu-
CC        := $(CROSS)gcc
PARTIALLD := $(CROSS)ld
OBJCOPY   := $(CROSS)objcopy
QEMU      := qemu-ppc64

CFLAGS := -O1 -g -std=gnu11 -Wall -fno-strict-aliasing -fwrapv -fvisibility=hidden -fno-common
CFLAGS += -Wno-unused-variable -Wno-unused-function -Wno-unused-but-set-variable
CFLAGS += -Werror=implicit-function-declaration -Werror=int-conversion
CFLAGS += -Werror=pointer-to-int-cast -Werror=int-to-pointer-cast
CFLAGS += -MMD -MP
CFLAGS += -D__PS3__ -DPS3_HOSTTEST -DYQ2OSTYPE=\"PS3host\" -DYQ2ARCH=\"ppc64\"
CFLAGS += -DNOUNCRYPT -DIOAPI_NO_64
CFLAGS += -DPS3_USRDIR=\"$(abspath $(DATA))\" -DPS3_DATADIR=\"$(HOME_DIR)\"

ifeq ($(STAGE),1)
CFLAGS += -DDEDICATED_ONLY -DPS3_STAGE1
endif

include $(SRC)/ps3/sources.mk

.PHONY: all run clean

all: q2host

$(OBJDIR)/engine/%.o: $(SRC)/%.c
	@mkdir -p $(dir $@)
	@echo "CC  $*.c"
	@$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/game/%.o: $(SRC)/%.c
	@mkdir -p $(dir $@)
	@echo "CC  [game] $*.c"
	@$(CC) $(CFLAGS) -c $< -o $@

$(GAME_O): $(GAME_OBJS)
	@echo "LD  game.o (only GetGameAPI stays global)"
	@$(PARTIALLD) -r -o $(OBJDIR)/game_all.o $(GAME_OBJS)
	@$(OBJCOPY) --keep-global-symbol=GetGameAPI $(OBJDIR)/game_all.o $@

q2host: $(ENGINE_OBJS) $(GAME_O)
	@echo "LD  $@"
	@$(CC) -static $(ENGINE_OBJS) $(GAME_O) -o $@ -lm -lpthread

-include $(ENGINE_OBJS:.o=.d) $(GAME_OBJS:.o=.d)

run: q2host
	@mkdir -p $(HOME_DIR)
	$(QEMU) ./q2host
	@echo "--- log: $(abspath $(DATA))/quake2_log.txt ---"

clean:
	rm -rf $(OBJDIR) q2host
