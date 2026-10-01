"""Build a private runtime against the installed SDK release, not its HEAD.

TWL code and ARM7 allocations must not rely on a loader being able to rewrite
locked MBK registers. Reserve 256 KiB in DSi main RAM instead. The ARM9 CRT
reserves that memory before constructors, not just on entry to main().
"""
from pathlib import Path
import subprocess
import difflib
import hashlib,json

root = Path(__file__).resolve().parents[1]
sdk = Path('/opt/wonderful/thirdparty/blocksds/core')
repo = root / 'investigation/upstream/sdk'
out = root / 'compat-runtime'
out.mkdir(exist_ok=True)
version = (sdk / 'version.txt').read_text().strip()
assert '1.24.0' in version, version

def release_file(path):
    frozen=root/'runtime-upstream'
    if (frozen/path).exists():
        data=(frozen/path).read_bytes()
        manifest=json.loads((frozen/'manifest.json').read_text())
        assert manifest['sdk_tag']=='v1.24.0'
        assert hashlib.sha256(data).hexdigest()==manifest['files'][path],path
        return data.decode()
    return subprocess.check_output(['git', '-C', str(repo), 'show',
                                    'v1.24.0:' + path], text=True)

original_link7 = release_file('sys/crts/ds_arm7.ld')
link7 = (sdk / 'sys/crts/ds_arm7.ld').read_text()
assert link7 == original_link7, 'Installed ARM7 linker differs from SDK v1.24.0'
assert 'twl_iwram : ORIGIN = 0x03000000' in link7
link7 = link7.replace('twl_iwram : ORIGIN = 0x03000000',
                      'twl_iwram : ORIGIN = 0x02d00000')
link7 = link7.replace('SECTIONS\n{', '''
__twl_heap_limit = ORIGIN(twl_iwram) + LENGTH(twl_iwram);
SECTIONS
{''')
link7 += '\nASSERT(__twl_end__ < __twl_heap_limit, "ARM7 TWL reserve exhausted");\n'
(out / 'arm7.ld').write_text(link7)

crt7 = release_file('sys/crts/ds_arm7_crt0.s')
assert '(0x03000000 + 256 * 1024)' in crt7
crt7 = crt7.replace('(0x03000000 + 256 * 1024)', '__twl_heap_limit')
(out / 'crt7.s').write_text(crt7)

crt9 = release_file('sys/crts/ds_arm9_crt0.s')
old = '    sub     r8, r8, 0xC000\n    str     r8, [r1]'
assert old in crt9
crt9 = crt9.replace(old, '''    sub     r8, r8, 0xC000
    // Reserve ARM7 TWL code/heap before any ARM9 constructor runs.
    // DS mode has only 4 MiB and therefore retains its existing heap limit.
    ldr     r2, =0x02d00000
    cmp     r8, r2
    movhi   r8, r2
    str     r8, [r1]''')
(out / 'crt9.s').write_text(crt9)
(out / 'main7.c').write_text(release_file('sys/arm7/main_core/source/main.c'))
for cpu in (7, 9):
    specs = (sdk / f'sys/crts/ds_arm{cpu}.specs').read_text()
    specs = specs.replace(f'%:getenv(BLOCKSDS /sys/crts/ds_arm{cpu}_crt0%O)',
                          str(out / f'crt{cpu}.o'))
    if cpu == 7:
        specs = specs.replace('%:getenv(BLOCKSDS /sys/crts/ds_arm7.ld)',
                              str(out / 'arm7.ld'))
    (out / f'arm{cpu}.specs').write_text(specs)
print('Private SDK v1.24.0 runtime:', out)
patch = []
for upstream_path, private_file in [
        ('sys/crts/ds_arm7.ld', 'arm7.ld'),
        ('sys/crts/ds_arm7_crt0.s', 'crt7.s'),
        ('sys/crts/ds_arm9_crt0.s', 'crt9.s')]:
    patch.extend(difflib.unified_diff(
        release_file(upstream_path).splitlines(keepends=True),
        (out / private_file).read_text().splitlines(keepends=True),
        fromfile='a/' + upstream_path, tofile='b/' + upstream_path))
(out / 'runtime.patch').write_text(''.join(patch))
