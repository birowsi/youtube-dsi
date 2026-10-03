"""HQ2-only ARM7: DSi volume and battery for the ARM9, plus a heartbeat.

The ARM7 reads both I2C registers in its main loop with interrupts disabled, once
a second, so the reads cannot interleave with libnds' own I2C use in interrupt
handlers. A request on FIFO_USER_08 is answered at once from the cached values:
    bits 0-7 volume (0-31), bits 8-15 battery (getBatteryLevel), bit 16 valid,
    bits 17-31 how many unsafe Wifi_MACRead() lengths were rejected.
The ARM9 never waits for an answer. The ARM9 also sends (as an address message on the
same channel) a word in main RAM that the ARM7 main loop increments every VBlank; the
ARM9 watchdog reads it to tell a dead ARM7 from a blocked FIFO. (An earlier version
used the IPC sync register, which the TWiLight/nds-bootstrap loader watches on real
hardware: the DSi froze within a second.)

The shared compat-runtime/main7.c and arm7.elf used by the hardware-verified HQ and
Compat builds stay unchanged; this writes compat-runtime/main7-hq2.c next to them.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / "compat-runtime/main7.c").read_text()

handler = '''
// HQ2: cached DSi volume/battery for the ARM9 (see tools/prepare_hq2_arm7.py).
#define FIFO_VOLUME FIFO_USER_08
static volatile u32 hq2_status;
static volatile u32 *hq2_beat;

static void beat_address(void *address, void *userdata)
{
    (void)userdata;
    hq2_beat = (volatile u32 *)address;
}

static void volume_request(u32 value, void *userdata)
{
    (void)value;
    (void)userdata;
    fifoSendValue32(FIFO_VOLUME, hq2_status);
}

// DSWiFi's Wifi_MACRead() passes the length to DMA unchecked: count = (length+1)/2.
// A zero-length received frame makes the count 0, which the DMA treats as 65536
// halfwords (128 KB), and a negative length is worse. On hardware this wiped the
// ARM9 heap right after DSWiFi's RX buffer (thread contexts and stacks) and froze
// or crashed playback after seconds to minutes. Linked with --wrap=Wifi_MACRead.
void __real_Wifi_MACRead(u16 *dest, u32 MAC_Base, u32 MAC_Offset, int length);
static volatile u32 hq2_macread_rejected;

void __wrap_Wifi_MACRead(u16 *dest, u32 MAC_Base, u32 MAC_Offset, int length)
{
    if (length <= 0 || length > 2400)
    {
        hq2_macread_rejected++;
        return;
    }
    __real_Wifi_MACRead(dest, MAC_Base, MAC_Offset, length);
}

static void hq2_refresh_status(void)
{
    if (!isDSiMode())
        return;
    // I2C transactions must not interleave; libnds does its own in IRQ handlers.
    int ime = enterCriticalSection();
    u32 volume = i2cReadRegister(I2C_PM, I2CREGPM_VOL) & 0xFF;
    u32 battery = getBatteryLevel() & 0xFF;
    leaveCriticalSection(ime);
    u32 rejected = hq2_macread_rejected > 0x7FFF ? 0x7FFF : hq2_macread_rejected;
    hq2_status = volume | (battery << 8) | (1u << 16) | (rejected << 17);
}

int main(void)
'''
anchor_main = "\nint main(void)\n"
anchor_fifo = "    installSystemFIFO(); // Sleep mode, storage, firmware...\n"
anchor_loop = "        swiWaitForVBlank();\n    }\n"
assert source.count(anchor_main) == 1 and source.count(anchor_fifo) == 1 and source.count(anchor_loop) == 1
patched = source.replace(anchor_main, handler, 1)
patched = patched.replace(anchor_fifo, anchor_fifo + "    fifoSetValue32Handler(FIFO_VOLUME, volume_request, 0);\n"
                          "    fifoSetAddressHandler(FIFO_VOLUME, beat_address, 0);\n", 1)
patched = patched.replace(anchor_loop, '''        swiWaitForVBlank();

        // Heartbeat for the ARM9 watchdog, in a word the ARM9 gave us.
        static u32 beat;
        beat++;
        if (hq2_beat)
            *hq2_beat = beat;
        if (beat % 60 == 1)
            hq2_refresh_status();
    }
''', 1)
(root / "compat-runtime/main7-hq2.c").write_text(patched)
print("compat-runtime/main7-hq2.c")
