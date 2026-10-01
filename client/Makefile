BLOCKSDS ?= /opt/wonderful/thirdparty/blocksds/core
NAME ?= YouTubeDSiCompat
GAME_TITLE := YouTube DSi
GAME_SUBTITLE := Search and Wi-Fi playback
GAME_AUTHOR := hanbi
ifneq ($(filter YouTubeDSiCompat YouTubeDSiHQ,$(NAME)),)
ARM7ELF ?= ../compat-runtime/arm7.elf
ifeq ($(NAME),YouTubeDSiHQ)
SPECS ?= ../compat-runtime/arm9-hq.specs
else
SPECS ?= ../compat-runtime/arm9.specs
endif
DEFINES += -DCOMPAT_WIFI
else
ARM7ELF ?= $(BLOCKSDS)/sys/arm7/main_core/arm7_dswifi_maxmod.elf
endif
ifeq ($(NAME),YouTubeDSiHQ)
DEFINES += -DQUALITY_STREAM
GAME_TITLE := YouTube DSi HQ
GAME_SUBTITLE := Adaptive picture and stereo
endif
LIBS := -ldswifi9 -lmm9 -lnds9
LIBDIRS := $(BLOCKSDS)/libs/dswifi $(BLOCKSDS)/libs/maxmod
ifeq ($(NAME),YouTubeDSiDiag)
DEFINES += -DBOOT_DIAGNOSTICS
endif
include $(BLOCKSDS)/sys/default_makefiles/rom_arm9/Makefile
