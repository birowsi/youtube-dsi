"""HQ2-only ARM7: DSi volume and battery for the ARM9, and DSWiFi fixes.

The ARM7 reads both I2C registers in its main loop with interrupts disabled, once
a second, so the reads cannot interleave with libnds' own I2C use in interrupt
handlers. A request on FIFO_USER_08 is answered at once from the cached values:
    bits 0-7 volume (0-31), bits 8-15 battery (getBatteryLevel), bit 16 valid.
The ARM9 never waits for an answer.

DSWiFi fixes (prebuilt library, so by --wrap): Wifi_Update() runs with interrupts
disabled, so the FIFO call can no longer be re-entered by the VBlank or Wi-Fi
interrupt; Wifi_MACRead()/Wifi_MACWrite() refuse lengths that would make a
65536-halfword DMA.

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

void __wrap_Wifi_MACRead(u16 *dest, u32 MAC_Base, u32 MAC_Offset, int length)
{
    if (length <= 0 || length > 2400)
        return;
    __real_Wifi_MACRead(dest, MAC_Base, MAC_Offset, length);
}

// DSWiFi's FIFO handler calls Wifi_Update() for every packet the ARM9 queues, and
// libnds runs FIFO handlers with interrupts enabled (IME=1). The VBlank handler and
// the Wi-Fi interrupt (TX/RX complete) then ran DSWiFi's update and TX flush in the
// middle of it. Wifi_TxArm9QueueFlush() reads the next TX size outside its critical
// section: when the nested call had already sent that packet, the outer call read
// the empty slot (size 0), made a 0-count DMA (65536 halfwords over all of MAC RAM)
// and left the TX read index 4 bytes off, so no ARM9 packet was sent again. The
// network stopped (no ACK, no ping) while the radio stayed associated.
// The VBlank and Wi-Fi interrupt handlers already run with IME=0; this makes the FIFO
// call the same. Linked with --wrap=Wifi_Update.
void __real_Wifi_Update(void);

void __wrap_Wifi_Update(void)
{
    int ime = enterCriticalSection();
    __real_Wifi_Update();
    leaveCriticalSection(ime);
}

// Same guard for TX: a 0 length would again be a 65536-halfword DMA over MAC RAM.
// Linked with --wrap=Wifi_MACWrite.
void __real_Wifi_MACWrite(const u16 *src, u32 MAC_Base, int length);

void __wrap_Wifi_MACWrite(const u16 *src, u32 MAC_Base, int length)
{
    if (length <= 0)
        return;
    __real_Wifi_MACWrite(src, MAC_Base, length);
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
    hq2_status = volume | (battery << 8) | (1u << 16);
}

int main(void)
'''
anchor_main = "\nint main(void)\n"
anchor_fifo = "    installSystemFIFO(); // Sleep mode, storage, firmware...\n"
anchor_loop = "        swiWaitForVBlank();\n    }\n"
assert source.count(anchor_main) == 1 and source.count(anchor_fifo) == 1 and source.count(anchor_loop) == 1
patched = source.replace(anchor_main, handler, 1)
patched = patched.replace(anchor_fifo, anchor_fifo + "    fifoSetValue32Handler(FIFO_VOLUME, volume_request, 0);\n", 1)
patched = patched.replace(anchor_loop, '''        swiWaitForVBlank();

        // Volume and battery, read once a second.
        static u32 frames;
        if (frames++ % 60 == 0)
            hq2_refresh_status();
    }
''', 1)
(root / "compat-runtime/main7-hq2.c").write_text(patched)
print("compat-runtime/main7-hq2.c")
