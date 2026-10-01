BLOCKSDS ?= /opt/wonderful/thirdparty/blocksds/core
NAME := YouTubeDSiConnect
GAME_TITLE := YouTube DSi connect check
GAME_SUBTITLE := RAM boot and live Wi-Fi status
GAME_AUTHOR := Local diagnostic
ARM7ELF := ../hardware-runtime/arm7_probe.elf
SPECS := ../hardware-runtime/arm9.specs
LIBS := -ldswifi9 -lnds9
LIBDIRS := $(BLOCKSDS)/libs/dswifi
include $(BLOCKSDS)/sys/default_makefiles/rom_arm9/Makefile
