#include <nds.h>
#include <dswifi9.h>
#include <stdio.h>

static volatile unsigned phase, aps, status, cancel_request, finished, connected, init_done;
static volatile u32 * const probe = (volatile u32 *)0x0c37f000;
static PrintConsole upper, lower;

static void ascii_ssid(char *out, const volatile unsigned char *ssid, unsigned len) {
    if (len > 32) len = 32;
    for (unsigned i=0; i<len; i++) out[i] = ssid[i]>=32 && ssid[i]<127 ? ssid[i] : '.';
    out[len] = 0;
}

static void scan_display(void) {
    consoleSelect(&upper);
    char ssid[33];
    ascii_ssid(ssid, (volatile unsigned char *)&probe[20], probe[16]);
    printf("\x1b[0;0HWi-Fi scan / RAM build 3\nSaved SSID:                    \n%-31.31s\n\nCard ready: %lu  Mode: %lu/%lu\nIRQ: %-8lu Poll: %-8lu\nPolled packets: %-8lu\n\n",
           init_done ? ssid : "waiting", (unsigned long)probe[10],
           (unsigned long)probe[8], (unsigned long)probe[9],
           (unsigned long)probe[13], (unsigned long)probe[11], (unsigned long)probe[14]);
    int found = init_done ? Wifi_GetNumAP() : 0;
    printf("Visible APs: %-3d               \n", found);
    for (unsigned i=0; i<6; i++) {
        Wifi_AccessPoint ap;
        if ((int)i < found && Wifi_GetAPData(i, &ap) == WIFI_RETURN_OK) {
            ascii_ssid(ssid, (const volatile unsigned char *)ap.ssid, ap.ssid_len);
            printf("%-31.31s\nch:%-2u %-5s saved:%u          \n", ssid, ap.channel,
                   Wifi_ApSecurityTypeString(ap.security_type),
                   !!(ap.flags & WFLAG_APDATA_CONFIG_IN_WFC));
        } else printf("                               \n                               \n");
    }
    consoleSelect(&lower);
}

static const char *stage_name(unsigned stage) {
    switch (stage) {
        case 0x10: return "ARM7 main loop";
        case 0x20: return "Wi-Fi init / clock";
        case 0x22: return "DSi settings init";
        case 0x23: return "Reading DS Wi-Fi settings";
        case 0x24: return "DS settings read";
        case 0x25: return "Reading DSi Wi-Fi settings";
        case 0x26: return "DSi settings read";
        case 0x27: return "Settings init returned";
        case 0x28: return "ARM7 init returned";
        case 0x30: return "Radio start";
        case 0x31: return "SDIO controller init";
        case 0x32: return "SDIO controller ready";
        case 0x33: return "Radio firmware start";
        case 0x34: return "Radio firmware ready";
        case 0x38: return "Radio init returned";
        default: return "Startup";
    }
}

static int connection_worker(void *arg) {
    (void)arg;
    phase = 1;
    if (!Wifi_InitDefault(INIT_ONLY | WIFI_ATTEMPT_DSI_MODE)) {
        phase = 6; finished = 1; return 1;
    }
    phase = 2;
    init_done = 1;
    aps = Wifi_GetData(WIFIGETDATA_NUMWFCAPS, 0, NULL);
    if (cancel_request || aps == 0) {
        phase = aps == 0 ? 7 : 8; finished = 1; return 1;
    }
    phase = 3;
    Wifi_AutoConnect();
    phase = 4;
    for (unsigned frames = 0; frames < 60*40 && !cancel_request; frames++) {
        status = Wifi_AssocStatus();
        if (status == ASSOCSTATUS_ASSOCIATED) {
            connected = 1; phase = 5; finished = 1; return 0;
        }
        if (status == ASSOCSTATUS_CANNOTCONNECT) break;
        cothread_yield_irq(IRQ_VBLANK);
    }
    Wifi_DisconnectAP();
    phase = cancel_request ? 8 : 9;
    finished = 1;
    return 1;
}

int main(void) {
    extern char *fake_heap_end;
    fake_heap_end = (char *)0x02d00000;
    defaultExceptionHandler(); lcdMainOnTop(); setBrightness(3, 0);
    videoSetMode(MODE_0_2D); vramSetBankA(VRAM_A_MAIN_BG);
    consoleInit(&upper, 0, BgType_Text4bpp, BgSize_T_256x256, 31, 0, true, true);
    videoSetModeSub(MODE_0_2D); vramSetBankC(VRAM_C_SUB_BG);
    consoleInit(&lower, 0, BgType_Text4bpp, BgSize_T_256x256, 31, 0, false, true);
    consoleSelect(&lower);
    printf("YouTube DSi CONNECT CHECK\nRAM build 3 / DSi mode: %s\n\nA: start Wi-Fi check\n\nShows saved SSID and APs.\nAuto RX polling if stalled.\n\nNo SD / keyboard / playback\n", isDSiMode() ? "yes" : "NO");
    while (REG_KEYINPUT & KEY_A) cothread_yield_irq(IRQ_VBLANK);
    while (!(REG_KEYINPUT & KEY_A)) cothread_yield_irq(IRQ_VBLANK);
    consoleClear();
    if (cothread_create(connection_worker, NULL, 8192, COTHREAD_DETACHED) < 0) {
        printf("Could not create worker\n"); while (1) cothread_yield_irq(IRQ_VBLANK);
    }
    unsigned frames=0, presses=0, last_keys=0;
    while (1) {
        cothread_yield_irq(IRQ_VBLANK);
        unsigned keys = (~REG_KEYINPUT) & (KEY_A | KEY_B | KEY_START);
        unsigned down = keys & ~last_keys; last_keys = keys;
        if (down & KEY_A) presses++;
        if (down & KEY_B) cancel_request = 1;
        if ((down & KEY_START) && finished) return 0;
        if ((frames++ % 30) && !down) continue;
        const char *label = "waiting";
        switch (phase) {
            case 1: label="INIT_ONLY / ARM7 wait"; break;
            case 2: label="Saved settings loaded"; break;
            case 3: label="Radio start / AutoConnect"; break;
            case 4: label="AP auth / DHCP"; break;
            case 5: label="CONNECTED"; break;
            case 6: label="INIT FAILED"; break;
            case 7: label="NO SAVED AP FOUND"; break;
            case 8: label="CANCELLED"; break;
            case 9: label="CONNECT FAILED / TIMEOUT"; break;
        }
        scan_display();
        printf("\x1b[0;0HYouTube DSi CONNECT CHECK\nRAM build 3\nSeconds: %-5u A presses: %-3u\nPhase: %-2u\n%-31s\n\nA7 stage: %02lX                 \n%-31s\nSCFG7: %08lX                 \nCLK: %04lX SP: %08lX         \nSPI: %06lX len: %-4lu op: %lu\n\nSaved APs: %-3u  Status: %-2u\n%-31s\n",
               frames/60, presses, phase, label, (unsigned long)probe[0],
               stage_name(probe[0]), (unsigned long)probe[1], (unsigned long)probe[2],
               (unsigned long)probe[6], (unsigned long)probe[3],
               (unsigned long)probe[4], (unsigned long)probe[5], aps, status,
               phase >= 4 && phase <= 5 && status <= ASSOCSTATUS_CANNOTCONNECT ? ASSOCSTATUS_STRINGS[status] : "");
        if (connected) {
            u32 ip = Wifi_GetIP();
            printf("IP: %lu.%lu.%lu.%lu            \n", (unsigned long)(ip & 255),
                   (unsigned long)((ip>>8)&255), (unsigned long)((ip>>16)&255),
                   (unsigned long)(ip>>24));
        } else printf("IP: waiting                    \n");
        printf("\nA: button test\nB: request cancel\n%-31s\n%-31s\n",
               finished ? "START: return to loader" : "Waiting driver; no forced reset",
               cancel_request && !finished ? "Cancel pending in driver" :
               frames > 60*30 && !finished ? "Driver wait exceeds 30 seconds" : "");
    }
}
