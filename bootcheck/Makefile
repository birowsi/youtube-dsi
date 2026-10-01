BLOCKSDS ?= /opt/wonderful/thirdparty/blocksds/core
NAME := YouTubeDSiBoot
GAME_TITLE := YouTube DSi boot check
GAME_SUBTITLE := Screen and buttons only
GAME_AUTHOR := Local diagnostic
ARM7ELF := $(BLOCKSDS)/sys/arm7/main_core/arm7_minimal.elf
ifeq ($(NAME),YouTubeDSiCore)
ARM7ELF := $(BLOCKSDS)/sys/arm7/main_core/arm7_dswifi_maxmod.elf
DEFINES += -DBOOT_COMBO
endif
ifeq ($(NAME),YouTubeDSiWifi)
ARM7ELF := $(BLOCKSDS)/sys/arm7/main_core/arm7_dswifi.elf
DEFINES += -DBOOT_WIFI
endif
ifeq ($(NAME),YouTubeDSiAudio)
ARM7ELF := $(BLOCKSDS)/sys/arm7/main_core/arm7_maxmod.elf
DEFINES += -DBOOT_AUDIO
endif
LIBS := -lnds9
include $(BLOCKSDS)/sys/default_makefiles/rom_arm9/Makefile
