// SPDX-License-Identifier: Zlib
// SPDX-FileNotice: Modified from the original version by the BlocksDS project.
//
// Copyright (C) 2005 Michael Noland (joat)
// Copyright (C) 2005 Jason Rogers (Dovoto)
// Copyright (C) 2005-2015 Dave Murphy (WinterMute)
// Copyright (C) 2023 Antonio Niño Díaz

// Default ARM7 core

#include <nds.h>


#if defined(USE_MAXMOD) && defined(USE_LIBXM7)
#error "Only one audio library can be used"
#endif

#if defined(USE_DSWIFI)
#include <dswifi7.h>
#endif

#if defined(USE_MAXMOD)
#include <maxmod7.h>
#endif

#if defined(USE_LIBXM7)
#include <libxm7.h>

// Assign FIFO_USER_07 channel to LibXM7
#define FIFO_LIBXM7 FIFO_USER_07

void XM7_Value32Handler(u32 command, void *userdata)
{
    (void)userdata;

    XM7_ModuleManager_Type *module = (XM7_ModuleManager_Type *)command;

    if (module == NULL)
        XM7_StopModule();
    else
        XM7_PlayModule(module);
}
#endif

volatile bool exit_loop = false;

void power_button_callback(void)
{
    exit_loop = true;
}

void vblank_handler(void)
{
    inputGetAndSend();
#if defined(USE_DSWIFI)
    Wifi_Update();
#endif
}

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
{
#if defined(DEBUG_LIBS)
    // The ARM9 exception handler can trap data aborts and undefined instruction
    // exceptions. However, the ARM7 handler can only trap undefined
    // instructions. When an exception happens, a message is sent to the ARM9 to
    // display the crash information.
    defaultExceptionHandler();
#endif

    // Initialize sound hardware
    // Required for some functions within libnds
    enableSound();

    // Read user information from the firmware (name, birthday, etc)
    readUserSettings();

    // Stop LED blinking
    ledBlink(0);

    // Using the calibration values read from the firmware with
    // readUserSettings(), calculate some internal values to convert raw
    // coordinates into screen coordinates.
    touchInit();

    irqInit();
    fifoInit();

#if defined(USE_DSWIFI)
    installWifiFIFO();
#endif

    // Required for some functions within libnds
    installSoundFIFO();

    installSystemFIFO(); // Sleep mode, storage, firmware...
    fifoSetValue32Handler(FIFO_VOLUME, volume_request, 0);
    fifoSetAddressHandler(FIFO_VOLUME, beat_address, 0);
    if (isDSiMode())
        installCameraFIFO();

#if defined(USE_MAXMOD)
    // Initialize Maxmod. It uses timer 0 internally.
    mmInstall(FIFO_MAXMOD);
#endif

#if defined(USE_LIBXM7)
    // Initialize LibXM7. It uses timer 0 internally.
    XM7_Initialize();
    // Setup the FIFO handler for LibXM7
    fifoSetValue32Handler(FIFO_LIBXM7, XM7_Value32Handler, 0);
#endif

    // This sets a callback that is called when the power button in a DSi
    // console is pressed. It has no effect in a DS.
    setPowerButtonCB(power_button_callback);

    // Read current date from the RTC and setup an interrupt to update the time
    // regularly. The interrupt simply adds one second every time, it doesn't
    // read the date. Reading the RTC is very slow, so it's a bad idea to do it
    // frequently.
    initClockIRQTimer(LIBNDS_DEFAULT_TIMER_RTC);

    // Now that the FIFO is setup we can start sending input data to the ARM9.
    irqSet(IRQ_VBLANK, vblank_handler);
    irqEnable(IRQ_VBLANK);

    while (!exit_loop)
    {
        const uint16_t key_mask = KEY_SELECT | KEY_START | KEY_L | KEY_R;
        uint16_t keys_pressed = ~REG_KEYINPUT;

        if ((keys_pressed & key_mask) == key_mask)
            exit_loop = true;

        swiWaitForVBlank();

        // Heartbeat for the ARM9 watchdog, in a word the ARM9 gave us.
        static u32 beat;
        beat++;
        if (hq2_beat)
            *hq2_beat = beat;
        if (beat % 60 == 1)
            hq2_refresh_status();
    }

    return 0;
}
