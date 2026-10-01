// YouTubeDSi prototype, local bridge protocol v1.
// Platform setup follows BlocksDS CC0 Wi-Fi, keyboard and Maxmod examples.
#include <nds.h>
#include <dswifi9.h>
#include <maxmod9.h>
#include <fat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#ifdef QUALITY_STREAM
#include "hq_player.h"
#endif

#define W 128
#define H 96
#define FPS 8
#define RATE 16000
#define SAMPLES 2000
#define VBYTES (W * H)
#define ABYTES (SAMPLES * 2)
#define PACKET (VBYTES + ABYTES)
#define QUEUE 16
#define AUDIO_SIZE 65536

static char host[64] = "192.168.0.4";
#ifdef QUALITY_STREAM
static int port = 8767;
#else
static int port = 8765;
#endif
static char error_text[440];
static char ids[8][12], titles[8][101];
static int results, selected;
static PrintConsole bottom;
#ifndef QUALITY_STREAM
static uint8_t packets[QUEUE][PACKET] __attribute__((aligned(32)));
static uint8_t audio_ring[AUDIO_SIZE] __attribute__((aligned(32)));
static volatile unsigned audio_in, audio_out, audio_count, played_samples;
static int video_bg;
#endif
static Keyboard *keyboard;
static void tick(void);

#ifdef COMPAT_WIFI
static volatile unsigned wifi_finished, wifi_connected, wifi_status, wifi_cancel, wifi_saved_aps;

static int wifi_worker(void *unused) {
    (void)unused;
    // The local official ftpd v3.2.1 reference uses NTR Wi-Fi. The version
    // running on the physical DSi has not been independently identified.
    // Keep DSi execution/memory; select only the legacy radio interface.
    if (Wifi_InitDefault(INIT_ONLY | WIFI_DS_MODE_ONLY)) {
        wifi_saved_aps = Wifi_GetData(WIFIGETDATA_NUMWFCAPS, 0, NULL);
        if (!wifi_saved_aps) { wifi_finished = 1; return 0; }
        Wifi_AutoConnect();
        for (unsigned frames = 0; frames < 40 * 60 && !wifi_cancel; frames++) {
            wifi_status = Wifi_AssocStatus();
            if (wifi_status == ASSOCSTATUS_ASSOCIATED) {
                wifi_connected = 1;
                break;
            }
            if (wifi_status == ASSOCSTATUS_CANNOTCONNECT) break;
            cothread_yield_irq(IRQ_VBLANK);
        }
    }
    if (!wifi_connected) Wifi_DisconnectAP();
    wifi_finished = 1;
    return 0;
}

static int connect_saved_wifi(void) {
    if (cothread_create(wifi_worker, NULL, 8192, COTHREAD_DETACHED) < 0) return 0;
    unsigned frames = 0;
    while (!wifi_finished) {
        tick();
        if (keysDown() & KEY_B) wifi_cancel = 1;
        if (frames++ % 30 == 0)
            printf("\x1b[14;0HWi-Fi: %-22s\nTime: %u seconds\nB: cancel\n",
                   wifi_status <= ASSOCSTATUS_CANNOTCONNECT ?
                   ASSOCSTATUS_STRINGS[wifi_status] : "initializing", frames / 60);
    }
    return wifi_connected;
}
#endif

// lwIP runs in a cooperative thread; BIOS waits alone starve network work.
static void tick(void) { cothread_yield_irq(IRQ_VBLANK); scanKeys(); }

static void wait_a(void) {
    printf("\nA: continue");
    do { tick(); } while (!(keysDown() & KEY_A));
}

#ifndef QUALITY_STREAM
static void reset_video(void) {
    memset(bgGetGfxPtr(video_bg), 0, 128 * 128);
}
#endif

static int type_text(const char *label, char *text, unsigned size) {
    unsigned length = strlen(text);
    keyboardShow();
    while (1) {
        tick();
        consoleClear();
        printf("%s\n\n%s_\n\nSTART: accept  B: cancel", label, text);
        if (keysDown() & KEY_B) { keyboardHide(); return 0; }
        if (keysDown() & KEY_START) { keyboardHide(); return length > 0; }
        int key = keyboardUpdate();
        if (key == '\n' || key == '\r') { keyboardHide(); return length > 0; }
        if (key == '\b' && length) text[--length] = 0;
        else if (key >= 32 && key < 127 && length + 1 < size) {
            text[length++] = key; text[length] = 0;
        }
    }
}

static void load_config(void) {
    if (!fatInitDefault()) return;
    const char *paths[] = {"sd:/youtube-dsi.ini", "/youtube-dsi.ini"};
    FILE *file = NULL;
    for (unsigned i = 0; i < 2 && !file; i++) file = fopen(paths[i], "r");
    if (!file) return;
    char line[128];
    while (fgets(line, sizeof(line), file)) {
        char value[64]; int number;
        if (sscanf(line, "host=%63s", value) == 1) snprintf(host, sizeof(host), "%s", value);
#ifdef QUALITY_STREAM
        // Keep the working legacy relay/config unchanged on port8765.
        if (sscanf(line, "quality_port=%d", &number) == 1 && number > 0 && number < 65536) port = number;
#else
        if (sscanf(line, "port=%d", &number) == 1 && number > 0 && number < 65536) port = number;
#endif
    }
    fclose(file);
}

static int open_request(const char *command) {
    error_text[0] = 0;
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        snprintf(error_text, sizeof(error_text), "Enter an IPv4 address"); return -1;
    }
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { snprintf(error_text, sizeof(error_text), "socket error %d", errno); return -1; }
    printf("Connecting to PC...\n");
    // libnds/lwIP socket operations yield internally while waiting.
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        snprintf(error_text, sizeof(error_text), "Can't reach %s:%d\nCheck PC server/firewall", host, port);
        close(fd); return -1;
    }
    printf("Sending request...\n");
    unsigned length = strlen(command), done = 0;
    while (done < length) {
        int n = send(fd, command + done, length - done, 0);
        if (n <= 0) { close(fd); snprintf(error_text, sizeof(error_text), "Send failed"); return -1; }
        done += n;
    }
    printf("Waiting for PC...\n");
    int opt = 1;
    ioctl(fd, FIONBIO, &opt);
    return fd;
}

static int receive_bytes(int fd, void *data, unsigned size) {
    unsigned used = 0, idle = 0;
    while (used < size) {
        int n = recv(fd, (uint8_t *)data + used, size - used, 0);
        if (n > 0) { used += n; idle = 0; continue; }
        if (n == 0) { snprintf(error_text, sizeof(error_text), "Server closed connection"); return 0; }
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            snprintf(error_text, sizeof(error_text), "Receive failed: %d", errno); return 0;
        }
        tick();
        if (keysDown() & KEY_B) { snprintf(error_text, sizeof(error_text), "Cancelled"); return 0; }
        if (++idle > 180 * 60) { snprintf(error_text, sizeof(error_text), "Server timeout"); return 0; }
    }
    return 1;
}

static int receive_line(int fd, char *line, unsigned size) {
    for (unsigned i = 0; i + 1 < size; i++) {
        if (!receive_bytes(fd, line + i, 1)) return 0;
        if (line[i] == '\n') { line[i] = 0; return 1; }
    }
    snprintf(error_text, sizeof(error_text), "Response line too long"); return 0;
}

static int search(const char *query) {
    char command[256], line[440] = "";
    snprintf(command, sizeof(command), "SEARCH %s\n", query);
    consoleClear(); printf("Searching YouTube...\n\nB: cancel\n");
    int fd = open_request(command);
    if (fd < 0) return 0;
    int ok = receive_line(fd, line, sizeof(line));
    if (ok && sscanf(line, "OK %d", &results) == 1 && results >= 0 && results <= 8) {
        for (int i = 0; i < results; i++) {
            if (!receive_line(fd, line, sizeof(line))) { ok = 0; break; }
            char *tab = strchr(line, '\t');
            if (!tab || tab - line != 11) { ok = 0; break; }
            *tab = 0;
            memcpy(ids[i], line, 11); ids[i][11] = 0;
            snprintf(titles[i], sizeof(titles[i]), "%.100s", tab + 1);
        }
    } else {
        ok = 0; snprintf(error_text, sizeof(error_text), "%.430s", line);
    }
    close(fd);
    selected = 0;
    return ok;
}

#ifndef QUALITY_STREAM
static mm_word audio_callback(mm_word length, mm_addr dest, mm_stream_formats format) {
    (void)format;
    unsigned wanted = length * 2;
    unsigned available = audio_count;
    unsigned take = wanted < available ? wanted : available;
    take &= ~1u;
    unsigned first = AUDIO_SIZE - audio_out;
    if (first > take) first = take;
    memcpy(dest, audio_ring + audio_out, first);
    memcpy((uint8_t *)dest + first, audio_ring, take - first);
    if (take < wanted) memset((uint8_t *)dest + take, 0, wanted - take);
    audio_out = (audio_out + take) % AUDIO_SIZE;
    audio_count -= take;
    played_samples += take / 2;
    return length;
}

static void add_audio(const uint8_t *pcm) {
    // Producer/IRQ consumer update counters atomically.
    unsigned first = AUDIO_SIZE - audio_in;
    if (first > ABYTES) first = ABYTES;
    memcpy(audio_ring + audio_in, pcm, first);
    memcpy(audio_ring, pcm + first, ABYTES - first);
    int irq = enterCriticalSection();
    audio_in = (audio_in + ABYTES) % AUDIO_SIZE;
    audio_count += ABYTES;
    leaveCriticalSection(irq);
}

static void draw_frame(const uint8_t *frame) {
    // VRAM only supports halfword/word writes; use halfword rows.
    uint16_t *dest = (uint16_t *)bgGetGfxPtr(video_bg);
    const uint16_t *source = (const uint16_t *)frame;
    for (unsigned y = 0; y < H; y++)
        for (unsigned x = 0; x < W / 2; x++) dest[y * 128 / 2 + x] = source[y * W / 2 + x];
}

static int playback(const char *id) {
    char command[64], line[440] = "";
    if (id) snprintf(command, sizeof(command), "PLAY %s\n", id);
    else snprintf(command, sizeof(command), "TEST\n");
    consoleClear(); printf("Preparing stream...\n\nB: cancel\n");
    int fd = open_request(command);
    if (fd < 0) return 0;
    if (!receive_line(fd, line, sizeof(line))) { close(fd); return 0; }
    if (strcmp(line, "OK STREAM")) {
        snprintf(error_text, sizeof(error_text), "%.430s", line); close(fd); return 0;
    }
    uint32_t header[7];
    if (!receive_bytes(fd, header, sizeof(header))) { close(fd); return 0; }
    if (memcmp(header, "YDS1", 4) || header[1] != W || header[2] != H ||
        header[3] != FPS || header[4] != RATE || header[5] != SAMPLES || header[6] != 1) {
        snprintf(error_text, sizeof(error_text), "Stream version mismatch"); close(fd); return 0;
    }
    audio_in = audio_out = audio_count = played_samples = 0;
    unsigned received = 0, released = 0, partial = 0;
    unsigned shown = ~0u, idle = 0;
    int started = 0, paused = 0, eof = 0, failed = 0;
    mm_stream stream = {.sampling_rate=RATE, .buffer_length=512, .callback=audio_callback,
                       .format=MM_STREAM_16BIT_MONO, .timer=MM_TIMER0, .manual=false};
    consoleClear();
    printf("Buffering...\n128x96 / 8fps / 16kHz\n\nA: pause/resume\nB: return\n");
    reset_video();
    while (1) {
        scanKeys();
        if (keysDown() & KEY_B) break;
        if ((keysDown() & KEY_A) && started) {
            paused = !paused;
            if (paused) mmStreamClose(); else mmStreamOpen(&stream);
            printf("\x1b[8;0H%s     ", paused ? "Paused" : "Playing");
        }
        int progressed = 0;
        if (!eof && received - released < QUEUE && audio_count <= AUDIO_SIZE - ABYTES) {
            int n = recv(fd, packets[received % QUEUE] + partial, PACKET - partial, 0);
            if (n > 0) {
                partial += n; idle = 0; progressed = 1;
                if (partial == PACKET) {
                    add_audio(packets[received % QUEUE] + VBYTES);
                    received++; partial = 0;
                }
            } else if (n == 0) { eof = 1; }
            else if (errno != EAGAIN && errno != EWOULDBLOCK) { eof = 1; failed = 1; }
        }
        if (!started && (received >= 6 || (eof && received))) {
            started = 1; mmStreamOpen(&stream); printf("\x1b[0;0HPlaying...  ");
        }
        if (started && !paused) {
            unsigned current = played_samples / SAMPLES;
            if (current >= received && received) current = received - 1;
            if (current != shown && current < received) {
                draw_frame(packets[current % QUEUE]); shown = current; released = current;
                printf("\x1b[7;0H%u:%02u  buffer:%u ", current / (FPS * 60),
                       (current / FPS) % 60, received - current);
            }
            if (eof && audio_count == 0) break;
        }
        if (eof && !received) { failed = 1; break; }
        if (!paused && ++idle > 60 * 30) { failed = 1; break; }
        if (progressed) cothread_yield();
        else cothread_yield_irq(IRQ_VBLANK);
    }
    if (started && !paused) mmStreamClose();
    close(fd); reset_video();
    if (failed || (eof && partial)) {
        snprintf(error_text, sizeof(error_text), "Stream interrupted. Check PC log."); return 0;
    }
    return 1;
}
#else
static int playback(const char *id) {
    char command[64], line[440]="";
    if(id)snprintf(command,sizeof(command),"PLAY2 %s\n",id);
    else snprintf(command,sizeof(command),"TEST2\n");
    consoleClear();printf("Preparing HQ stream...\n\nB: cancel\n");
    int fd=open_request(command);
    if(fd<0)return 0;
    if(!receive_line(fd,line,sizeof(line)) || strcmp(line,"OK STREAM")) {
        if(line[0])snprintf(error_text,sizeof(error_text),"%.430s",line);
        close(fd);return 0;
    }
    int ok=hq_playback(fd,error_text,sizeof(error_text));
    close(fd);return ok;
}
#endif

static void show_error(void) {
    consoleClear(); printf("%s\n", error_text[0] ? error_text : "Request failed"); wait_a();
}

int main(void) {
#ifdef HARDWARE_RUNTIME
    // ARM7 owns 256 KB of main RAM starting here; keep ARM9 allocations below it.
    extern char *fake_heap_end;
    fake_heap_end = (char *)0x02d00000;
#endif
    defaultExceptionHandler();
    lcdMainOnTop();
    setBrightness(3, 0);
    // Show progress before filesystem, keyboard or Wi-Fi initialization.
    videoSetModeSub(MODE_0_2D);
    vramSetBankC(VRAM_C_SUB_BG);
    consoleInit(&bottom, 0, BgType_Text4bpp, BgSize_T_256x256, 22, 3, false, true);
    consoleSelect(&bottom);
    printf("YouTube DSi boot\nDSi mode: %s\n", isDSiMode() ? "yes" : "NO");
#ifdef QUALITY_STREAM
    if(!isDSiMode()) { printf("HQ playback needs DSi mode\n");wait_a();return 1; }
#endif
#ifdef QUALITY_STREAM
    printf("HQ build 1 / NTR Wi-Fi\n");
#elif defined(COMPAT_WIFI)
    printf("COMPAT build 1 / NTR Wi-Fi\n");
#endif
#ifdef BOOT_DIAGNOSTICS
#ifdef HARDWARE_RUNTIME
    printf("HARDWARE RAM build 1\n\nARM7 TWL: main RAM\n");
#endif
    printf("DIAGNOSTIC build 2\n\n1: ARM9 reached main\nSD config: skipped\n\nA: initialize app\n");
    wait_a();
#endif
    printf("2: video initialization\n");
#ifdef QUALITY_STREAM
    hq_video_init();
#else
    videoSetMode(MODE_5_2D);
    vramSetBankA(VRAM_A_MAIN_BG);
    video_bg = bgInit(3, BgType_Bmp8, BgSize_B8_128x128, 0, 0);
    bgSetScale(video_bg, 128, 128); bgUpdate();
    for (unsigned i = 0; i < 256; i++)
        BG_PALETTE[i] = RGB5(((i >> 5) & 7) * 31 / 7, ((i >> 2) & 7) * 31 / 7, (i & 3) * 31 / 3);
    reset_video();
#endif
    // The console reserves the keyboard's tile/map area (tiles 0, maps 20-21).
    printf("3: keyboard initialization\n");
    keyboard = keyboardDemoInit();
    keyboardHide();
#ifndef BOOT_DIAGNOSTICS
    printf("4: reading SD config\n");
    load_config();
#else
    // Isolate SD/loader access issues. This PC's default host/port still work.
    (void)load_config;
#endif
    printf("YouTube DSi prototype\n\nDSi mode: %s\n\nConnecting saved Wi-Fi...\n",
           isDSiMode() ? "yes" : "NO");
#ifdef COMPAT_WIFI
    int wifi_ok = connect_saved_wifi();
#else
    int wifi_ok = Wifi_InitDefault(WFC_CONNECT | WIFI_ATTEMPT_DSI_MODE);
#endif
    if (!wifi_ok) {
#ifdef COMPAT_WIFI
        consoleClear();
        printf("Wi-Fi connection failed\nStatus: %u\nSaved compatible APs: %u\n\nUse a saved DS-compatible AP.\nOpen/WEP networks supported.\n", wifi_status, wifi_saved_aps);
#else
        printf("\nWi-Fi failed. Configure a\n2.4GHz WPA2 connection in\nDSi settings, then retry.\n");
#endif
        wait_a(); return 1;
    }
    printf("5: Wi-Fi connected\n6: audio initialization\n");
    mm_ds_system sys = {.mod_count=0, .samp_count=0, .mem_bank=0, .fifo_channel=FIFO_MAXMOD};
    if (!mmInit(&sys)) { printf("Audio init failed\n"); wait_a(); return 1; }
    while (1) {
        consoleClear();
#ifdef QUALITY_STREAM
        printf("YouTube DSi HQ\n\nServer: %s\nPort: %d\n\nA: search YouTube\nX: picture + stereo test\nY: change server IP\nSTART: exit\n\n256x192 / 16fps\n32kHz stereo / auto quality\nPC HQ relay must be running\n", host, port);
#else
        printf("YouTube DSi\n\nServer: %s\nPort: %d\n\nA: search YouTube\nX: test video + sound\nY: change server IP\nSTART: exit\n\nLow-resolution prototype\nPC bridge must be running\n", host, port);
#endif
        unsigned keys;
        do { tick(); keys = keysDown(); } while (!(keys & (KEY_A|KEY_X|KEY_Y|KEY_START)));
        if (keys & KEY_START) break;
        if (keys & KEY_Y) { type_text("Server IPv4 address", host, sizeof(host)); continue; }
        if (keys & KEY_X) { if (!playback(NULL)) show_error(); continue; }
        char query[180] = "";
        if (!type_text("Search YouTube", query, sizeof(query))) continue;
        if (!search(query)) { show_error(); continue; }
        int back = 0;
        while (!back) {
            consoleClear();
            printf("%s\n%d results\n\n", query, results);
            if (!results) printf("No results\n");
            int page = selected / 3;
            for (int i = page * 3; i < results && i < page * 3 + 3; i++)
                printf("%c%d %.27s\n  %.29s\n\n", i == selected ? '>' : ' ', i+1, titles[i],
                       strlen(titles[i]) > 27 ? titles[i]+27 : "");
            printf("\x1b[20;0HUP/DOWN: select\nA: play   B: back\n");
            do { tick(); keys = keysDown(); } while (!(keys & (KEY_UP|KEY_DOWN|KEY_A|KEY_B|KEY_TOUCH)));
            if ((keys & KEY_UP) && selected > 0) selected--;
            if ((keys & KEY_DOWN) && selected+1 < results) selected++;
            if (keys & KEY_TOUCH) {
                touchPosition touch; touchRead(&touch);
                int row = ((touch.py / 8) - 3) / 3;
                int choice = page * 3 + row;
                if (touch.py >= 24 && row >= 0 && row < 3 && choice < results) {
                    selected = choice;
                    if (!playback(ids[selected])) show_error();
                }
            }
            if ((keys & KEY_A) && results && !playback(ids[selected])) show_error();
            if (keys & KEY_B) back = 1;
        }
    }
    Wifi_DisconnectAP(); Wifi_DisableWifi();
    return 0;
}
