"""HQ2-only ARM7: answer volume requests (DSi volume register, 0-31) on FIFO_USER_08.

The shared compat-runtime/main7.c and arm7.elf used by the hardware-verified HQ and
Compat builds stay unchanged; this writes compat-runtime/main7-hq2.c next to them.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / "compat-runtime/main7.c").read_text()

handler = '''
// HQ2: the ARM9 asks for the DSi volume; reply 0-31, or 0xFF outside DSi mode.
// Runs in the FIFO handler, the same context libnds uses for its battery I2C reads.
#define FIFO_VOLUME FIFO_USER_08
static void volume_request(u32 value, void *userdata)
{
    (void)value;
    (void)userdata;
    fifoSendValue32(FIFO_VOLUME, isDSiMode() ? i2cReadRegister(I2C_PM, I2CREGPM_VOL) : 0xFF);
}

int main(void)
'''
anchor_main = "\nint main(void)\n"
anchor_fifo = "    installSystemFIFO(); // Sleep mode, storage, firmware...\n"
assert source.count(anchor_main) == 1 and source.count(anchor_fifo) == 1
patched = source.replace(anchor_main, handler, 1)
patched = patched.replace(anchor_fifo, anchor_fifo + "    fifoSetValue32Handler(FIFO_VOLUME, volume_request, 0);\n", 1)
(root / "compat-runtime/main7-hq2.c").write_text(patched)
print("compat-runtime/main7-hq2.c")
