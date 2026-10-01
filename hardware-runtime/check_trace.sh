#!/bin/sh
# Emulator-only negative control: stop ARM7 before main and verify ARM9 trace.
set -eu
export BLOCKSDS=/opt/wonderful/thirdparty/blocksds/core
export PATH=/opt/wonderful/toolchain/gcc-arm-none-eabi/bin:/usr/bin:/bin
cd "$(dirname "$0")"
python3 ../tools/prepare_hardware_runtime.py
sed "s@$(pwd)/crt7.o@$(pwd)/forced-crt7.o@g" arm7.specs > forced-arm7.specs
arm-none-eabi-gcc -DBOOT_FORCE_TRACE_STOP -c -x assembler-with-cpp \
    -mcpu=arm7tdmi -o forced-crt7.o crt7.s
arm-none-eabi-gcc -mthumb -mcpu=arm7tdmi -specs="$PWD/forced-arm7.specs" \
    -L"$BLOCKSDS/libs/libnds/lib" -L"$BLOCKSDS/libs/dswifi/lib" \
    -L"$BLOCKSDS/libs/maxmod/lib" -o forced-arm7.elf main7.o \
    -Wl,--start-group -lnds7 -ldswifi7 -lmm7 -lc -Wl,--end-group
"$BLOCKSDS/tools/ndstool/ndstool" -c trace-probe.nds \
    -7 forced-arm7.elf -9 ../client/build/YouTubeDSiHardware.elf
../tools/emutest-build/emutest trace-probe.nds ../emulator-trace-proof 300 dsi \
    > ../emulator-trace-proof-run.log 2>&1
