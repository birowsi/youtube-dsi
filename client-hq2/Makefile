# YouTubeDSi HQ2: steady-picture YDS3 stream + DSi-style lower screen.
# Separate folder so the hardware-verified client/ sources stay untouched.
# Assets (Hangul font table, top-screen art, banner icon): python3 ../tools/make_hq2_assets.py
BLOCKSDS ?= /opt/wonderful/thirdparty/blocksds/core
NAME ?= YouTubeDSiHQ2
# DSi menu / TWiLight banner: three lines, ASCII (system banner fonts lack Hangul).
GAME_TITLE := YouTube DSi HQ2
GAME_SUBTITLE := Steady picture + stereo
GAME_AUTHOR := hanbi
# Animated DSi icon: user-made 32x32, 5 frames x 150 ms, magenta (palette 0) = transparent.
# Source: NDSi/icons/yeoltaeng.gif. DS mode shows the first frame.
GAME_ICON := assets/yeoltaeng.gif
# Header: game code YTHQ, maker 00, internal title (max 12 chars).
NDSTOOL_ARGS := -g YTHQ 00 YOUTUBEDSI2
ARM7ELF ?= ../compat-runtime/arm7-hq2.elf
SPECS ?= ../compat-runtime/arm9-hq.specs
DEFINES += -DCOMPAT_WIFI -DQUALITY_STREAM
LIBS := -ldswifi9 -lmm9 -lnds9
# Every cothread (including DSWiFi's lwIP threads) gets a 32 KB stack: source/net_stacks.c
LDFLAGS := -Wl,--wrap=cothread_create -Wl,--wrap=aligned_alloc
LIBDIRS := $(BLOCKSDS)/libs/dswifi $(BLOCKSDS)/libs/maxmod
include $(BLOCKSDS)/sys/default_makefiles/rom_arm9/Makefile
