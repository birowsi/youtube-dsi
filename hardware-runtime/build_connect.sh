#!/bin/sh
set -eu
export BLOCKSDS=/opt/wonderful/thirdparty/blocksds/core
export PATH=/opt/wonderful/toolchain/gcc-arm-none-eabi/bin:/usr/bin:/bin
cd "$(dirname "$0")"
: "${ROM_NAME:=YouTubeDSiConnect3}"
: "${PROBE_CFLAGS:=}"
python3 ../tools/prepare_hardware_runtime.py
arm-none-eabi-gcc -c -x assembler-with-cpp -mcpu=arm7tdmi -o crt7.o crt7.s
arm-none-eabi-gcc -c -x assembler-with-cpp -mcpu=arm946e-s -o crt9.o crt9.s
arm-none-eabi-gcc -c -marm -mcpu=arm946e-s -O2 -ffreestanding -fno-builtin -o trace.o trace.c
arm-none-eabi-gcc -mthumb -mcpu=arm7tdmi -O2 -ffunction-sections -fdata-sections \
    -DUSE_DSWIFI=1 -DUSE_MAXMOD=1 -DWIFI_POLL_BUILD -specs="$PWD/arm7.specs" \
    -I"$BLOCKSDS/libs/libnds/include" -I"$BLOCKSDS/libs/dswifi/include" \
    -I"$BLOCKSDS/libs/maxmod/include" -c main7.c -o main7.o
arm-none-eabi-gcc -mthumb -mcpu=arm7tdmi -O2 -ffunction-sections -fdata-sections \
    $PROBE_CFLAGS -specs="$PWD/arm7.specs" -I"$BLOCKSDS/libs/libnds/include" \
    -I../tools/dswifi-src/source -I"$BLOCKSDS/libs/dswifi/include" \
    -c wifi_probe7.c -o wifi_probe7.o
arm-none-eabi-gcc -mthumb -mcpu=arm7tdmi -specs="$PWD/arm7.specs" \
    -L"$BLOCKSDS/libs/libnds/lib" -L"$BLOCKSDS/libs/dswifi/lib" \
    -L"$BLOCKSDS/libs/maxmod/lib" -Wl,-Map,arm7_probe.map \
    -Wl,--wrap=Wifi_Init,--wrap=Wifi_TWL_Init,--wrap=Wifi_NTR_GetWfcSettings \
    -Wl,--wrap=Wifi_TWL_GetWfcSettings,--wrap=wifi_card_init \
    -Wl,--wrap=wifi_sdio_controller_init,--wrap=wifi_card_device_init,--wrap=readFirmware \
    -Wl,--wrap=Wifi_TWL_Update,--wrap=irqSetAUX,--wrap=irqEnableAUX \
    -o "arm7_${ROM_NAME}.elf" main7.o wifi_probe7.o \
    -Wl,--start-group -lnds7 -ldswifi7 -lmm7 -lc -Wl,--end-group
cd ../connectcheck
make NAME="$ROM_NAME" ARM7ELF="../hardware-runtime/arm7_${ROM_NAME}.elf"
