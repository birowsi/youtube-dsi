#!/bin/sh
set -eu
export BLOCKSDS=/opt/wonderful/thirdparty/blocksds/core
export PATH=/opt/wonderful/toolchain/gcc-arm-none-eabi/bin:/usr/bin:/bin
cd "$(dirname "$0")/.."
python3 tools/prepare_compat_runtime.py
cd compat-runtime
arm-none-eabi-gcc -c -x assembler-with-cpp -mcpu=arm7tdmi -o crt7.o crt7.s
arm-none-eabi-gcc -c -x assembler-with-cpp -mcpu=arm946e-s -o crt9.o crt9.s
arm-none-eabi-gcc -mthumb -mcpu=arm7tdmi -O2 -ffunction-sections -fdata-sections \
  -DUSE_DSWIFI=1 -DUSE_MAXMOD=1 -specs="$PWD/arm7.specs" \
  -I"$BLOCKSDS/libs/libnds/include" -I"$BLOCKSDS/libs/dswifi/include" \
  -I"$BLOCKSDS/libs/maxmod/include" -c main7.c -o main7.o
arm-none-eabi-gcc -mthumb -mcpu=arm7tdmi -specs="$PWD/arm7.specs" \
  -L"$BLOCKSDS/libs/libnds/lib" -L"$BLOCKSDS/libs/dswifi/lib" \
  -L"$BLOCKSDS/libs/maxmod/lib" -Wl,-Map,arm7.map \
  -o arm7.elf main7.o -Wl,--start-group -lnds7 -ldswifi7 -lmm7 -lc -Wl,--end-group
cd ../client
# Other historical variants share client/build; rebuild to prevent stale flags.
make -B NAME=YouTubeDSiCompat DEFINES=-DCOMPAT_WIFI \
  ARM7ELF=../compat-runtime/arm7.elf SPECS=../compat-runtime/arm9.specs
