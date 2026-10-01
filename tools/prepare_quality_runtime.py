"""Place large HQ ARM9 buffers beyond all normal ARM7 loader staging data."""
from pathlib import Path
import difflib,subprocess,sys
root=Path(__file__).resolve().parents[1]
subprocess.run([sys.executable,str(root/'tools/prepare_compat_runtime.py')],check=True)
out=root/'compat-runtime'
from prepare_compat_runtime import release_file
source=release_file('sys/crts/ds_arm9.ld')
old='.twl __end__ : AT(MAX(0x2400000,__end__))'
assert source.count(old)==1
patched=source.replace(old,'.twl MAX(0x2400000,__end__) : AT(MAX(0x2400000,__end__))')
patched+='\nASSERT(__twl_end__ < 0x02d00000, "HQ ARM9 buffers overlap ARM7 reserve");\n'
(out/'arm9-hq.ld').write_text(patched)
specs=(out/'arm9.specs').read_text()
old_spec='%:getenv(BLOCKSDS /sys/crts/ds_arm9.ld)'
assert old_spec in specs
(out/'arm9-hq.specs').write_text(specs.replace(old_spec,str(out/'arm9-hq.ld')))
(out/'quality-layout.patch').write_text(''.join(difflib.unified_diff(
    source.splitlines(keepends=True),patched.splitlines(keepends=True),
    fromfile='a/sys/crts/ds_arm9.ld',tofile='b/sys/crts/ds_arm9.ld')))
