#ifndef YT_UI_H
#define YT_UI_H
#include <stdint.h>

// Lower-screen interface for YouTubeDSi HQ2. Visual language follows DSi Desk
// (dsi-dashboard/DESIGN.md): light system surfaces, thin rules, Galmuri9.
#define UI_TITLE_W 232
#define UI_TITLE_H 22
#define UI_TITLE_BYTES (UI_TITLE_W / 8 * UI_TITLE_H)

void ui_init(void);
void ui_top_splash(const char *host, int port);
void ui_top_stop(void);
void ui_top_tick(void);  // call once per VBlank while menus are shown

void ui_status(const char *title, const char *line1, const char *line2, const char *hint);
void ui_error(const char *message);
// Exception screen: plain text lines, written straight to VRAM without DMA.
void ui_crash(const char *const *lines, int count);

void ui_home(const char *host, int port, int pressed);
int ui_home_hit(int x, int y);  // 1 search, 2 test, 3 server IP, 0 none

#define UI_RESULTS_PER_PAGE 3
// SEARCH5 title bitmaps wrap at this width; the video length goes to their right.
#define UI_RESULT_TITLE_W 188
// One line with the channel name under each title (SEARCH5).
#define UI_CHANNEL_W 232
#define UI_CHANNEL_H 12
#define UI_CHANNEL_BYTES (UI_CHANNEL_W / 8 * UI_CHANNEL_H)
#define UI_CHANNEL_LEN 61
// bitmaps/channel_bitmaps: one per result, or NULL to draw the text instead.
// durations: seconds per result (0 unknown, -1 live).
void ui_results(const char *query, int count, int selected, int pressed,
                const char (*titles)[101], const uint8_t *bitmaps, const int *durations,
                const char (*channels)[UI_CHANNEL_LEN], const uint8_t *channel_bitmaps);
int ui_results_hit(int x, int y, int selected, int count);  // index, or -1

#define UI_RECENT_MAX 8
#define UI_RECENT_LEN 240
// Edits UTF-8 `text` in place; Hangul (dubeolsik) input when hangul_allowed.
// recent: earlier searches, newest first, or NULL. Up or the Recent key lists them;
// picking one returns 1 with it in `text`. X there deletes one and lowers *recent_count.
int ui_keyboard(const char *label, char *text, unsigned size, void (*tick)(void), int hangul_allowed,
                char (*recent)[UI_RECENT_LEN], int *recent_count);

typedef struct {
    const uint8_t *title_bitmap;  // UI_TITLE_BYTES, or NULL
    const char *title_text;
    int mode;                     // 0 starting, 1 playing, 2 buffering, 3 paused
    unsigned seconds, buffer_ms;
    unsigned quality, picture_fps, net_kib, decode_ms, late, rebuffers, gaps;
    int pressed;                  // 1 pause, 2 back
    int show_stats;
    int volume;                   // DSi volume 0-31, -1 unknown
    int duration;                 // seconds; 0 unknown (no position bar)
    int seek_target;              // seconds, while Left/Right is choosing; -1 none
    const char *notice;           // warning line (e.g. network stalled), or NULL
} PlayerView;
void ui_player(const PlayerView *view);
int ui_player_hit(int x, int y);  // 1 pause/resume, 2 back, 3 stats, 4 position bar, 0 none
int ui_player_bar_seconds(int x, int duration);

#endif
