"""Read-only analysis of the user's official Bunjalloo 0.12.0 ROM.

The ROM is preserved. Optional emulator runs use synthetic loader profiles;
they are not a dump of the physical DSi's state or proof of physical Wi-Fi.
Run under WSL with --emulate to reproduce the NWRAM alias hypothesis.
"""
from pathlib import Path
import concurrent.futures
import hashlib
import json
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'youtube-dsi/investigation/bunjalloo'
ROM = ROOT / 'roms/tools/bunjalloo.nds'
EXPECTED = '68af2969fde46ae5047f16c68d28a84801c8794ac2544f062bb19c1132b63b0b'
OUT.mkdir(parents=True, exist_ok=True)
b = ROM.read_bytes()
assert hashlib.sha256(b).hexdigest() == EXPECTED
official = OUT / 'official-v012.nds'
assert official.read_bytes() == b, 'Local ROM differs from downloaded release'

def u32(off):
    return struct.unpack_from('<I', b, off)[0]

def cpu(off):
    return dict(zip(('rom', 'entry', 'ram', 'size'),
                    [hex(u32(off + 4 * i)) for i in range(4)]))

a7 = b[u32(0x30):u32(0x30) + u32(0x3c)]
word = lambda off: struct.unpack_from('<I', a7, off)[0]
# Actual CRT instructions and the literal pool they load, checked against
# arm-none-eabi-objdump. This older CRT predates the MOD7 section tables.
expected_instructions = {
    0x6c: 0xe35a0001,  # cmp r10,#1
    0x70: 0x1a000009,  # bne NotTWL at 0x9c
    0x84: 0xe59f20a0,  # TWL copy destination -> literal 0x12c
    0x88: 0xe59f30a0,  # TWL copy length -> literal 0x130
    0x90: 0xe59f009c,  # TWL BSS start -> literal 0x134
    0x94: 0xe59f109c,  # TWL BSS length -> literal 0x138
}
for off, instruction in expected_instructions.items():
    assert word(off) == instruction, (hex(off), hex(word(off)))
vma, size, bss_start, bss_size = [word(x) for x in (0x12c, 0x130, 0x134, 0x138)]
assert (vma, size, bss_start, bss_size) == (0x03000000, 0x7694, 0x03007694, 0x34f4)
report = {
    'sha256': EXPECTED, 'official_v012_identical': True, 'size': len(b),
    'source_tag_commit': 'da08e441276e25366112d1c4724832981cfdc7b5',
    'unit_code': b[0x12],
    'arm9': cpu(0x20), 'arm7': cpu(0x30),
    'arm9i': cpu(0x1c0), 'arm7i': cpu(0x1d0),
    'mbk_arm7': [hex(u32(x)) for x in (0x1a0, 0x1a4, 0x1a8)],
    'mbk9': hex(u32(0x1ac)), 'access': hex(u32(0x1b4)),
    'scfg_mask': hex(u32(0x1b8)),
    'crt_twl_runtime': {
        'vma': hex(vma), 'initialized_size': hex(size),
        'bss_start': hex(bss_start), 'bss_size': hex(bss_size),
        'bss_end': hex(bss_start + bss_size),
        'heap_start': hex(word(0x140)), 'heap_end': '0x03040000',
        'conditional': 'Skipped when ARM9 passes a DS-mode flag (r10 != 1)',
    },
    'conditional_32k_mirror_corruption': {
        'requires': '03000000 maps 32 KiB shared-WRAM mirror, rather than NWRAM-A',
        'zeroed_initialized_bytes': '03000000..03002B88 (end exclusive)',
        'physical_mapping_dumped': False,
    },
    'library_revision_boundary': 'SDK/DSWiFi release revisions are not pinned in the application Makefile; do not infer them from current upstream HEAD',
}
(OUT / 'binary-evidence.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2), flush=True)

if '--emulate' in sys.argv:
    runner = ROOT / 'youtube-dsi/tools/emutest-build/emutest'
    def run(case):
        name, mode, profile = case
        dest = OUT / name
        dest.mkdir(exist_ok=True)
        with (dest / 'run.log').open('w') as log:
            subprocess.run([str(runner), str(ROM), str(dest), '720', mode,
                            'none', profile], stdout=log,
                           stderr=subprocess.STDOUT, check=True)
        mem = (dest / 'memory.bin').read_bytes()
        result = {'mode': mode, 'profile': profile,
                  'cpu': (dest / 'cpu.txt').read_text(),
                  'first_32_at_03000000': mem[:32].hex(),
                  'first_32_zero': mem[:32] == bytes(32)}
        (dest / 'evidence.json').write_text(json.dumps(result, indent=2) + '\n')
        print(name, json.dumps(result), flush=True)
        return result
    cases = [('dsi-direct', 'dsi', 'direct'),
             ('dsi-legacy-mbk', 'dsi', 'legacy-mbk'),
             ('ds-direct', 'ds', 'direct')]
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        results = list(pool.map(run, cases))
    assert results[1]['first_32_zero'], 'Alias hypothesis did not reproduce'
