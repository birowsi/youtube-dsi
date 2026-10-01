#!/bin/sh
# Internal negative control: force ARM7 to stall in Wi-Fi settings init.
set -eu
export BLOCKSDS=/opt/wonderful/thirdparty/blocksds/core
export PATH=/opt/wonderful/toolchain/gcc-arm-none-eabi/bin:/usr/bin:/bin
cd "$(dirname "$0")"
arm-none-eabi-gcc -mthumb -mcpu=arm7tdmi -O2 -ffunction-sections -fdata-sections \
    -DPROBE_FORCE_INIT_WAIT -specs="$PWD/arm7.specs" \
    -I"$BLOCKSDS/libs/libnds/include" -c wifi_probe7.c -o forced_wifi_probe7.o
arm-none-eabi-gcc -mthumb -mcpu=arm7tdmi -specs="$PWD/arm7.specs" \
    -L"$BLOCKSDS/libs/libnds/lib" -L"$BLOCKSDS/libs/dswifi/lib" \
    -L"$BLOCKSDS/libs/maxmod/lib" \
    -Wl,--wrap=Wifi_Init,--wrap=Wifi_TWL_Init,--wrap=Wifi_NTR_GetWfcSettings \
    -Wl,--wrap=Wifi_TWL_GetWfcSettings,--wrap=wifi_card_init \
    -Wl,--wrap=wifi_sdio_controller_init,--wrap=wifi_card_device_init,--wrap=readFirmware \
    -o arm7_forced_wait.elf main7.o forced_wifi_probe7.o \
    -Wl,--start-group -lnds7 -ldswifi7 -lmm7 -lc -Wl,--end-group
"$BLOCKSDS/tools/ndstool/ndstool" -c forced-connect-wait.nds \
    -7 arm7_forced_wait.elf -9 ../connectcheck/build/YouTubeDSiConnect.elf
../tools/emutest-build/emutest forced-connect-wait.nds ../emulator-connect-wait 3600 dsi \
    > ../emulator-connect-wait-run.log 2>&1
