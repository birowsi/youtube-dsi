"""Generate a task-local BlocksDS runtime; never change the installed SDK.

Reserve 0x02D00000..0x02D40000 for ARM7 TWL code/data/heap. ARM9 must
cap its heap at 0x02D00000. Keep a visible pre-main trace during IPC waits.
"""
from pathlib import Path

sdk = Path('/opt/wonderful/thirdparty/blocksds/core')
out = Path(__file__).resolve().parents[1] / 'hardware-runtime'
out.mkdir(exist_ok=True)
crt = sdk / 'sys/crts'
crt_source = Path(__file__).resolve().parents[2] / 'reference/blocksds-reference/sys/crts'
for cpu in (7, 9):
    specs = (crt / f'ds_arm{cpu}.specs').read_text()
    specs = specs.replace(f'%:getenv(BLOCKSDS /sys/crts/ds_arm{cpu}_crt0%O)',
                          str(out / f'crt{cpu}.o'))
    if cpu == 7:
        specs = specs.replace('%:getenv(BLOCKSDS /sys/crts/ds_arm7.ld)', str(out / 'arm7.ld'))
    else:
        specs = specs.replace(str(out / 'crt9.o'), str(out / 'crt9.o') + ' ' + str(out / 'trace.o'))
    (out / f'arm{cpu}.specs').write_text(specs)

link = (crt / 'ds_arm7.ld').read_text()
link = link.replace('twl_iwram : ORIGIN = 0x03000000', 'twl_iwram : ORIGIN = 0x02d00000')
(out / 'arm7.ld').write_text(link)

a7 = (crt_source / 'ds_arm7_crt0.s').read_text()
a7 = a7.replace('(0x03000000 + 256 * 1024)', '(0x02d00000 + 256 * 1024)')
a7 = a7.replace('_start:\n', '_start:\n    trace7 1\n', 1)
for old, new in [
    ('    // Copy arm7 binary', '    trace7 2\n\n    // Copy arm7 binary'),
    ('    ldr     r0, =__bss_start__', '    trace7 3\n    ldr     r0, =__bss_start__'),
    ('    cmp     r10, #1             //', '    trace7 4\n    cmp     r10, #1             //'),
    ('    ldr     r0, =__twl_bss_start__', '    trace7 5\n#ifdef BOOT_FORCE_TRACE_STOP\n1:  b 1b\n#endif\n    ldr     r0, =__twl_bss_start__'),
    ('NotTWL:\n', 'NotTWL:\n    trace7 6\n'),
    ('    // Checks if the argv', '    trace7 7\n\n    // Checks if the argv'),
    ('    // Send 0x7 to the ARM9', '    trace7 8\n\n    // Send 0x7 to the ARM9'),
    ('    // Prepare address, arguments', '    trace7 9\n\n    // Prepare address, arguments'),
]:
    assert old in a7, old
    a7 = a7.replace(old, new, 1)
a7 = a7.replace('    .global  _start', '''    .global  _start
    .macro trace7 value
    ldr r4, =0x0237f000
    mov r5, #\\value
    str r5, [r4]
    .endm''')
(out / 'crt7.s').write_text(a7)

a9 = (crt_source / 'ds_arm9_crt0.s').read_text()
assert 'IPCSync:\n' in a9
a9 = a9.replace('IPCSync:\n', '''IPCSync:
    // Preserve caller scratch registers while rendering without libnds/BSS.
    push {r0-r4, r12, lr}
    sub sp, sp, #4
    bl early_boot_trace
    add sp, sp, #4
    pop {r0-r4, r12, lr}
''', 1)
(out / 'crt9.s').write_text(a9)

main = (crt_source.parent / 'arm7/main_core/source/main.c').read_text()
main = main.replace('#include <nds.h>', '#include <nds.h>\n#ifdef WIFI_POLL_BUILD\nvoid wifi_probe_tick(void);\n#endif', 1)
main = main.replace('        swiWaitForVBlank();', '        swiWaitForVBlank();\n#ifdef WIFI_POLL_BUILD\n        wifi_probe_tick();\n#endif', 1)
main = main.replace('    enableSound();', '    *(volatile u32 *)0x0237f000 = 10;\n    enableSound();')
for marker, call in [(11, 'readUserSettings();'), (12, 'touchInit();'),
                     (13, 'fifoInit();'), (14, 'installWifiFIFO();'),
                     (15, 'initClockIRQTimer(LIBNDS_DEFAULT_TIMER_RTC);')]:
    main = main.replace('    ' + call, f'    *(volatile u32 *)0x0237f000 = {marker};\n    ' + call)
main = main.replace('    while (!exit_loop)', '    *(volatile u32 *)0x0237f000 = 16;\n    while (!exit_loop)')
(out / 'main7.c').write_text(main)
