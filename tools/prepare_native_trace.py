"""Prepare opt-in, source-level TWL telemetry, without changing installed SDK.

The historical --wrap wifi_card_device_init hook misses same-TU calls. This
patch places the hook in the real function, so inlining and aliases retain it.
It is diagnostic only: no timeout, IRQ polling, register override or ROM is
installed. The compatibility release does not link this object.
"""
from pathlib import Path
import difflib
import subprocess

root = Path(__file__).resolve().parents[1]
repo = root / 'investigation/upstream/dswifi'
out = root / 'hardware-runtime/native-trace'
out.mkdir(parents=True, exist_ok=True)
pin = '02f193979c945b41386f5b5050f7cc426a999484'
path = 'source/arm7/twl/card.twl.c'
original = subprocess.check_output(['git', '-C', str(repo), 'show',
                                    pin + ':' + path], text=True)
text = original

def replace_once(old, new):
    global text
    assert text.count(old) == 1, old
    text = text.replace(old, new, 1)

replace_once('static wifi_card_ctx wlan_ctx = {0};', '''// This probe region belongs to the diagnostic build, not the release app.
#ifdef WIFI_TWL_TRACE
static inline void twl_trace(unsigned step, unsigned value)
{
    volatile u32 *p = (volatile u32 *)0x0237f000;
    p[19] = step;
    p[15] = value;
}
#define TWL_TRACE(step, value) twl_trace((step), (value))
#define TWL_STAGE(stage) (*(volatile u32 *)0x0237f000 = (stage))
#else
#define TWL_TRACE(step, value) ((void)0)
#define TWL_STAGE(stage) ((void)0)
#endif

static wifi_card_ctx wlan_ctx = {0};''')
replace_once('int wifi_card_device_init(void)\n{',
             'int wifi_card_device_init(void)\n{\n    TWL_STAGE(0x33);')
replace_once('    return wifi_card_wlan_init();', '''    int result = wifi_card_wlan_init();
    TWL_TRACE(0x34, (u32)result);
    TWL_STAGE(0x34);
    return result;''')
replace_once('    i2cWriteRegister(I2C_PM, 0x30, 0x13);', '''    TWL_TRACE(0x40, 0x3013);
    i2cWriteRegister(I2C_PM, 0x30, 0x13);
    TWL_TRACE(0x41, 0);''')
replace_once('                wifi_card_send_command(cmd5, ocr);', '''                TWL_TRACE(0x42, ocr);
                wifi_card_send_command(cmd5, ocr);
                TWL_TRACE(0x43, ctx->tmio.resp[0]);''')
replace_once('        if (wifi_card_read_func0_u8(0x3) == 0x02)', '''        TWL_TRACE(0x44, 3);
        if (wifi_card_read_func0_u8(0x3) == 0x02)''')
replace_once('    u32 bmi_ver = wifi_card_bmi_get_version();', '''    TWL_TRACE(0x45, device_chip_id);
    u32 bmi_ver = wifi_card_bmi_get_version();
    TWL_TRACE(0x46, bmi_ver);''')
replace_once('    wifi_card_bmi_start_firmware();', '''    TWL_TRACE(0x47, device_host_interest_addr);
    wifi_card_bmi_start_firmware();
    TWL_TRACE(0x48, 0);''')
replace_once('        u32 is_ready = wifi_card_read_intern_word(device_host_interest_addr + 0x58);', '''        TWL_TRACE(0x49, device_host_interest_addr + 0x58);
        u32 is_ready = wifi_card_read_intern_word(device_host_interest_addr + 0x58);
        TWL_TRACE(0x4a, is_ready);''')
replace_once('    wifi_card_bInitted = true;',
             '    TWL_TRACE(0x4b, device_eeprom_version);\n    wifi_card_bInitted = true;')
(out / 'card.twl.c').write_text(text)
(out / 'source-trace.patch').write_text(''.join(difflib.unified_diff(
    original.splitlines(keepends=True), text.splitlines(keepends=True),
    fromfile='a/' + path, tofile='b/' + path)))
cc = '/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-gcc'
flags = ['-g', '-std=gnu23', '-Wall', '-Wextra', '-D__NDS__', '-DARM7',
         '-mcpu=arm7tdmi', '-mthumb', '-Os', '-ffunction-sections',
         '-fdata-sections', '-fomit-frame-pointer',
         '-I/opt/wonderful/thirdparty/blocksds/core/libs/libnds/include']
for inc in ['include', 'source', 'mbedtls/include', 'source/arm7/twl/crypto']:
    flags += ['-I' + str(repo / inc)]
for name, defines in [('debug', ['-DWIFI_TWL_TRACE']), ('off', [])]:
    subprocess.run([cc, *flags, *defines, '-c', str(out / 'card.twl.c'),
                    '-o', str(out / ('card-' + name + '.o'))], check=True)
    objdump = str(Path(cc).with_name('arm-none-eabi-objdump'))
    disasm = subprocess.check_output([objdump, '-dr',
                                     str(out / ('card-' + name + '.o'))], text=True)
    (out / ('card-' + name + '-disasm.txt')).write_text(disasm)
    assert ('237f000' in disasm) == (name == 'debug')
print('Compiled opt-in source trace; debug contains probe, off contains none.')
