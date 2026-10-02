// YouTubeDSi HQ2: steady-picture YDS3 stream + DSi-style lower screen.
// Derived from the hardware-verified HQ client (client/source/main.c).
// Boot, SD config, NTR Wi-Fi worker and socket code are kept as they were;
// only the presentation layer and the PLAY3/SEARCH3 requests are new.
// Platform setup follows BlocksDS CC0 Wi-Fi and Maxmod examples.
#include <nds.h>
#include <dswifi9.h>
#include <maxmod9.h>
#include <fat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "hq_player.h"
#include "ui.h"

#ifndef QUALITY_STREAM
#error "HQ2 is a DSi-mode quality build"
#endif

static char host[64] = "192.168.0.4", ini_host[64];
static int port = 8767;
static char error_text[440];
static char ids[8][12], titles[8][101];
static uint8_t title_bitmaps[8][UI_TITLE_BYTES];
static int results, selected, have_bitmaps;
static void tick(void);

static volatile unsigned wifi_finished, wifi_connected, wifi_status, wifi_cancel, wifi_saved_aps;

static int wifi_worker(void *unused) {
    (void)unused;
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
    if (cothread_create(wifi_worker, NULL, 16 * 1024, COTHREAD_DETACHED) < 0) return 0;
    unsigned frames = 0;
    char line[48];
    while (!wifi_finished) {
        tick();
        if (keysDown() & KEY_B) wifi_cancel = 1;
        if (frames++ % 30 == 0) {
            snprintf(line, sizeof(line), "%s  -  %u s",
                     wifi_status <= ASSOCSTATUS_CANNOTCONNECT ?
                     ASSOCSTATUS_STRINGS[wifi_status] : "Starting", frames / 60);
            ui_status("YouTube DSi", "Connecting to saved Wi-Fi", line, "B: Cancel");
        }
    }
    return wifi_connected;
}

// lwIP runs in a cooperative thread; BIOS waits alone starve network work.
static void tick(void) { cothread_yield_irq(IRQ_VBLANK); scanKeys(); hq_lid_update(); ui_top_tick(); }

static void wait_a(void) {
    do { tick(); } while (!(keysDown() & (KEY_A | KEY_TOUCH)));
}

static void show_error(void) {
    ui_error(error_text[0] ? error_text : "Request failed");
    wait_a();
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
        // Keep the working legacy relay/config unchanged on port 8765.
        if (sscanf(line, "quality_port=%d", &number) == 1 && number > 0 && number < 65536) port = number;
    }
    fclose(file);
    snprintf(ini_host, sizeof(ini_host), "%s", host);
    // A PC address found by discovery is reused while the INI host is unchanged.
    file = fopen("/youtube-dsi.cache", "r");
    if (!file) return;
    char cached_ini[64] = "", cached_host[64] = "";
    while (fgets(line, sizeof(line), file)) {
        sscanf(line, "ini_host=%63s", cached_ini);
        sscanf(line, "host=%63s", cached_host);
    }
    fclose(file);
    if (cached_host[0] && !strcmp(cached_ini, ini_host)) snprintf(host, sizeof(host), "%s", cached_host);
}

static void save_found_host(void) {
    FILE *file = fopen("/youtube-dsi.cache", "w");
    if (!file) return;
    fprintf(file, "ini_host=%s\nhost=%s\n", ini_host, host);
    fclose(file);
}

// Broadcast "YTDSI?" to the quality relay port; the relay answers with its port and our nonce.
static int discover_pc(void) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return 0;
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
    int nonblock = 1;
    ioctl(fd, FIONBIO, &nonblock);
    struct sockaddr_in local = {0};
    local.sin_family = AF_INET;
    bind(fd, (struct sockaddr *)&local, sizeof(local));
    struct in_addr gateway, mask, dns1, dns2, ip = Wifi_GetIPInfo(&gateway, &mask, &dns1, &dns2);
    static unsigned counter;
    char nonce[20], probe[48];
    snprintf(nonce, sizeof(nonce), "%08x%08x", (unsigned)time(NULL) ^ (unsigned)ip.s_addr, ++counter * 2654435761u);
    snprintf(probe, sizeof(probe), "YTDSI?\t%s\n", nonce);
    uint32_t targets[2] = {ip.s_addr | ~mask.s_addr, INADDR_BROADCAST};
    int found = 0;
    for (int attempt = 0; attempt < 3 && !found; attempt++) {
        for (int i = 0; i < 2; i++) {
            struct sockaddr_in to = {0};
            to.sin_family = AF_INET;
            to.sin_port = htons(port);
            to.sin_addr.s_addr = targets[i];
            sendto(fd, probe, strlen(probe), 0, (struct sockaddr *)&to, sizeof(to));
        }
        for (unsigned frames = 0; frames < 30 && !found; frames++) {
            char reply[96], echoed[20];
            struct sockaddr_in from;
            socklen_t size = sizeof(from);
            int found_port, n = recvfrom(fd, reply, sizeof(reply) - 1, 0, (struct sockaddr *)&from, &size);
            if (n <= 0) { tick(); continue; }
            reply[n] = 0;
            if (sscanf(reply, "YTDSI!\t%d\t%19s", &found_port, echoed) == 2 && !strcmp(echoed, nonce) &&
                found_port > 0 && found_port < 65536 && inet_ntop(AF_INET, &from.sin_addr, host, sizeof(host))) {
                port = found_port;
                found = 1;
            }
        }
    }
    close(fd);
    if (found && strcmp(host, ini_host)) save_found_host();
    return found;
}

static int connect_pc(const char *title) {
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) return -1;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { snprintf(error_text, sizeof(error_text), "socket error %d", errno); return -2; }
    ui_status(title, "Connecting to the server...", host, "B: Cancel");
    // libnds/lwIP socket operations yield internally while waiting.
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { close(fd); return -1; }
    return fd;
}

static int open_request(const char *command, const char *title) {
    error_text[0] = 0;
    int fd = connect_pc(title);
    if (fd == -1) {
        // The PC may have a new address; look for the relay on the local network.
        ui_status(title, "Searching for the server...", NULL, "B: Cancel");
        if (discover_pc()) {
            ui_top_splash(host, port);
            fd = connect_pc(title);
        }
    }
    if (fd < 0) {
        if (fd == -1)
            snprintf(error_text, sizeof(error_text),
                     "Can't reach the server at %s:%d. Check that it is running on the same Wi-Fi.",
                     host, port);
        return -1;
    }
    unsigned length = strlen(command), done = 0;
    while (done < length) {
        int n = send(fd, command + done, length - done, 0);
        if (n <= 0) { close(fd); snprintf(error_text, sizeof(error_text), "Send failed"); return -1; }
        done += n;
    }
    ui_status(title, "Waiting for the server...", NULL, "B: Cancel");
    int opt = 1;
    ioctl(fd, FIONBIO, &opt);
    return fd;
}

static int receive_bytes(int fd, void *data, unsigned size) {
    unsigned used = 0, idle = 0;
    while (used < size) {
        int n = recv(fd, (uint8_t *)data + used, size - used, 0);
        if (n > 0) { used += n; idle = 0; continue; }
        if (n == 0) { snprintf(error_text, sizeof(error_text), "The server closed the connection."); return 0; }
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            snprintf(error_text, sizeof(error_text), "Receive failed: %d", errno); return 0;
        }
        tick();
        if (keysDown() & KEY_B) { snprintf(error_text, sizeof(error_text), "Cancelled"); return 0; }
        if (++idle > 180 * 60) { snprintf(error_text, sizeof(error_text), "The server did not answer."); return 0; }
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

static void server_error(const char *line) {
    if (!strncmp(line, "ERR Unknown command", 19))
        snprintf(error_text, sizeof(error_text),
                 "The server is an old version. Update it and start it again.");
    else snprintf(error_text, sizeof(error_text), "%.430s", line[0] ? line : "Request failed");
}

static int search(const char *query) {
    char command[264], line[440] = "";
    snprintf(command, sizeof(command), "SEARCH3 %s\n", query);
    int fd = open_request(command, "Searching");
    if (fd < 0) return 0;
    ui_status("Searching", "Searching YouTube...", query, "B: Cancel");
    int ok = receive_line(fd, line, sizeof(line)), w = 0, h = 0;
    have_bitmaps = 0;
    if (ok && sscanf(line, "OK %d %d %d", &results, &w, &h) >= 1 && results >= 0 && results <= 8) {
        for (int i = 0; i < results; i++) {
            if (!receive_line(fd, line, sizeof(line))) { ok = 0; break; }
            char *tab = strchr(line, '\t');
            if (!tab || tab - line != 11) { ok = 0; break; }
            *tab = 0;
            memcpy(ids[i], line, 11); ids[i][11] = 0;
            snprintf(titles[i], sizeof(titles[i]), "%.100s", tab + 1);
        }
        if (ok && w == UI_TITLE_W && h == UI_TITLE_H) {
            for (int i = 0; i < results && ok; i++)
                ok = receive_bytes(fd, title_bitmaps[i], UI_TITLE_BYTES);
            have_bitmaps = ok;
        }
    } else if (ok) {
        ok = 0; server_error(line);
    }
    close(fd);
    selected = 0;
    return ok;
}

static int playback(int index) {
    int start = 0;
    while (1) {
        char command[64], line[440] = "";
        // PLAY4 carries a start position; a seek reconnects from the new position.
        if (index >= 0) snprintf(command, sizeof(command), "PLAY4 %s %d\n", ids[index], start);
        else snprintf(command, sizeof(command), "TEST3\n");
        int fd = open_request(command, "Now Playing");
        if (fd < 0) return 0;
        ui_status("Now Playing", start ? "Moving to the new position..." : "Preparing the stream...",
                  "The server is opening the video", "B: Cancel");
        int duration = 0, from = 0;
        if (!receive_line(fd, line, sizeof(line)) ||
            (strcmp(line, "OK STREAM") && sscanf(line, "OK STREAM4 %d %d", &duration, &from) != 2)) {
            if (line[0]) server_error(line);
            close(fd); return 0;
        }
        ui_top_stop();
        int seek_to = -1;
        int ok = hq_playback(fd, error_text, sizeof(error_text),
                             index >= 0 && have_bitmaps ? title_bitmaps[index] : NULL,
                             index >= 0 ? titles[index] : "Picture + stereo test (440 Hz left, 660 Hz right)",
                             from, duration, &seek_to);
        close(fd);
        if (ok && seek_to >= 0) { start = seek_to; continue; }
        ui_top_splash(host, port);
        return ok;
    }
}

// Touch press: highlight on touch-down, act on release inside the same target.
static int touch_target(int (*hit)(int, int), int *pressed, void (*redraw)(int)) {
    if (keysDown() & KEY_TOUCH) {
        touchPosition t; touchRead(&t);
        int h = hit(t.px, t.py);
        if (h) { *pressed = h; redraw(h); }
    }
    if (*pressed && !(keysHeld() & KEY_TOUCH)) {
        int fired = *pressed; *pressed = 0; redraw(0);
        return fired;
    }
    return 0;
}
static void home_redraw(int pressed) { ui_home(host, port, pressed); }
static int result_hit(int x, int y) { return ui_results_hit(x, y, selected, results) + 1; }
static char last_query[240];
static void results_redraw(int pressed) {
    ui_results(last_query, results, selected, pressed - 1, titles, have_bitmaps ? title_bitmaps[0] : NULL);
}

static void results_loop(void) {
    int pressed = 0;
    results_redraw(0);
    while (1) {
        tick();
        unsigned keys = keysDown();
        int fired = touch_target(result_hit, &pressed, results_redraw);
        if (keys & KEY_B) return;
        int moved = 0;
        if ((keys & KEY_UP) && selected > 0) { selected--; moved = 1; }
        if ((keys & KEY_DOWN) && selected + 1 < results) { selected++; moved = 1; }
        if ((keys & KEY_L) && selected >= UI_RESULTS_PER_PAGE) { selected -= UI_RESULTS_PER_PAGE; moved = 1; }
        if ((keys & KEY_R) && results) {
            int next = (selected / UI_RESULTS_PER_PAGE + 1) * UI_RESULTS_PER_PAGE;
            if (next < results) { selected = next; moved = 1; }
        }
        if (moved) results_redraw(0);
        int play = -1;
        if (fired) { selected = fired - 1; play = selected; }
        if ((keys & KEY_A) && results) play = selected;
        if (play >= 0) {
            if (!playback(play)) show_error();
            results_redraw(0);
        }
    }
}

int main(void) {
#ifdef HARDWARE_RUNTIME
    // ARM7 owns 256 KB of main RAM starting here; keep ARM9 allocations below it.
    extern char *fake_heap_end;
    fake_heap_end = (char *)0x02d00000;
#endif
    setExceptionHandler(hq_crash_handler);
    // Closing the lid turns the screens off instead of sleeping, so Wi-Fi survives.
    disableSleep();
    lcdMainOnTop();
    setBrightness(3, 0);
    ui_init();
    if (!isDSiMode()) {
        ui_status("YouTube DSi", "HQ2 needs DSi mode", "Start it from TWiLight in DSi mode", "A: Exit");
        wait_a(); return 1;
    }
    ui_status("YouTube DSi", "Starting...", "Video", NULL);
    hq_video_init();
    ui_status("YouTube DSi", "Starting...", "Reading SD settings", NULL);
    load_config();
    ui_top_splash(host, port);
    int wifi_ok = connect_saved_wifi();
    if (!wifi_ok) {
        snprintf(error_text, sizeof(error_text),
                 "Wi-Fi connection failed (status %u, saved compatible APs %u). Use a saved DS-compatible "
                 "connection: Open or WEP networks.", wifi_status, wifi_saved_aps);
        show_error(); return 1;
    }
    ui_status("YouTube DSi", "Starting...", "Looking for the server", NULL);
    if (discover_pc()) ui_top_splash(host, port);
    ui_status("YouTube DSi", "Starting...", "Sound", NULL);
    mm_ds_system sys = {.mod_count=0, .samp_count=0, .mem_bank=0, .fifo_channel=FIFO_MAXMOD};
    if (!mmInit(&sys)) { snprintf(error_text, sizeof(error_text), "Audio init failed"); show_error(); return 1; }
    int pressed = 0;
    ui_home(host, port, 0);
    while (1) {
        tick();
        unsigned keys = keysDown();
        int action = touch_target(ui_home_hit, &pressed, home_redraw);
        if (keys & KEY_START) break;
        if (keys & KEY_A) action = 1;
        if (keys & KEY_X) action = 2;
        if (keys & KEY_Y) action = 3;
        if (action == 3) {
            char edit[64];
            snprintf(edit, sizeof(edit), "%s", host);
            if (ui_keyboard("Server address (IPv4)", edit, sizeof(edit), tick, 0)) {
                snprintf(host, sizeof(host), "%s", edit);
                ui_top_splash(host, port);
            }
        } else if (action == 2) {
            if (!playback(-1)) show_error();
        } else if (action == 1) {
            char query[240] = "";
            snprintf(query, sizeof(query), "%s", last_query);
            if (ui_keyboard("Search YouTube", query, sizeof(query), tick, 1)) {
                snprintf(last_query, sizeof(last_query), "%s", query);
                if (search(query)) results_loop();
                else show_error();
            }
        }
        if (action) ui_home(host, port, 0);
    }
    Wifi_DisconnectAP(); Wifi_DisableWifi();
    powerOn(PM_BACKLIGHT_TOP | PM_BACKLIGHT_BOTTOM);
    enableSleep();
    return 0;
}
