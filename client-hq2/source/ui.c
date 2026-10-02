// YouTubeDSi HQ2 lower-screen interface.
// Drawing approach and palette follow DSi Desk (dsi-dashboard/DESIGN.md):
// every surface is completed in a RAM buffer, then DMA-copied to VRAM.
// Icons are original glyphs drawn in C; no Nintendo or YouTube artwork.
#include <nds.h>
#include <stdio.h>
#include <string.h>
#include "ui.h"
#include "font.h"

#define W 256
#define H 192
#define C(r,g,b) (BIT(15)|RGB15(r,g,b))
#define BG C(30,30,29)
#define PAPER C(31,31,30)
#define CHROME C(27,27,26)
#define RULE C(21,21,20)
#define INK C(5,5,5)
#define MUTED C(13,13,12)
#define ACCENT C(28,14,3)
#define WARNING C(24,5,4)
#define WHITE C(31,31,31)

static uint16_t paint[W * H] __attribute__((aligned(32)));
static uint16_t *sub_vram;

static void rect(uint16_t *s, int x, int y, int w, int h, uint16_t color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > W) w = W - x;
    if (y + h > H) h = H - y;
    for (int yy = y; yy < y + h; yy++) {
        uint16_t *row = s + yy * W + x;
        for (int xx = 0; xx < w; xx++) row[xx] = color;
    }
}
static void outline(uint16_t *s, int x, int y, int w, int h, uint16_t color) {
    rect(s, x, y, w, 1, color); rect(s, x, y + h - 1, w, 1, color);
    rect(s, x, y, 1, h, color); rect(s, x + w - 1, y, 1, h, color);
}
// ---- Text: UTF-8, Galmuri9 at its native 10-pixel size --------------------
// ASCII uses the DSi Desk table; Hangul syllables and compatibility jamo use
// the generated Galmuri9 table (hangul_font.c). Anything else draws as '?'.
extern const uint16_t hangul_glyphs[11172][10];
extern const uint16_t jamo_glyphs[94][10];

static unsigned next_cp(const char **t) {
    const unsigned char *s = (const unsigned char *)*t;
    unsigned c = *s;
    if (c < 0x80) { *t += 1; return c; }
    if ((c & 0xe0) == 0xc0 && s[1]) { *t += 2; return ((c & 0x1f) << 6) | (s[1] & 0x3f); }
    if ((c & 0xf0) == 0xe0 && s[1] && s[2]) {
        *t += 3; return ((c & 0x0f) << 12) | ((s[1] & 0x3f) << 6) | (s[2] & 0x3f);
    }
    if ((c & 0xf8) == 0xf0 && s[1] && s[2] && s[3]) { *t += 4; return '?'; }
    *t += 1; return '?';
}
static const uint16_t *glyph(unsigned cp, int *advance) {
    if (cp >= 0xAC00 && cp <= 0xD7A3) { *advance = 10; return hangul_glyphs[cp - 0xAC00]; }
    if (cp >= 0x3131 && cp <= 0x318E) { *advance = 10; return jamo_glyphs[cp - 0x3131]; }
    if (cp < 32 || cp > 126) cp = '?';
    *advance = desk_font_width[cp - 32];
    return desk_font[cp - 32];
}
static int cp_width(unsigned cp) { int a; glyph(cp, &a); return a; }
static int draw_cp(uint16_t *s, int x, int y, unsigned cp, uint16_t color) {
    int advance;
    const uint16_t *rows = glyph(cp, &advance);
    for (int yy = 0; yy < 10; yy++) {
        unsigned bits = rows[yy];
        for (int xx = 0; bits; xx++, bits >>= 1)
            if ((bits & 1) && x + xx >= 0 && x + xx < W && y + yy >= 0 && y + yy < H)
                s[(y + yy) * W + x + xx] = color;
    }
    return advance;
}
// Optional integer scale is only used where a larger heading is wanted.
static void text_at(uint16_t *s, int x, int y, const char *t, uint16_t color, int scale) {
    (void)scale;
    while (*t) {
        unsigned cp = next_cp(&t);
        if (x + cp_width(cp) > W) break;
        x += draw_cp(s, x, y, cp, color);
    }
}
static int text_width(const char *t) {
    int width = 0;
    while (*t) width += cp_width(next_cp(&t));
    return width;
}
// Byte length of the longest prefix of t that fits in `width` pixels.
static int fit_bytes(const char *t, int width) {
    const char *p = t;
    int w = 0;
    while (*p) {
        const char *q = p;
        int cw = cp_width(next_cp(&q));
        if (w + cw > width) break;
        w += cw; p = q;
    }
    return p - t;
}
static void text_fit(uint16_t *s, int x, int y, const char *t, int width, uint16_t color) {
    if (text_width(t) <= width) { text_at(s, x, y, t, color, 1); return; }
    char line[256];
    int n = fit_bytes(t, width - text_width("..."));
    if (n > (int)sizeof(line) - 4) n = sizeof(line) - 4;
    memcpy(line, t, n); strcpy(line + n, "...");
    text_at(s, x, y, line, color, 1);
}
static void text_center(uint16_t *s, int cx, int y, const char *t, uint16_t color) {
    int w = text_width(t);
    if (w > 244) w = 244;
    text_fit(s, cx - w / 2, y, t, 244, color);
}
static void stamp(uint16_t *s, int x, int y, const uint16_t rows[16], uint16_t color) {
    for (int yy = 0; yy < 16; yy++)
        for (int xx = 0; xx < 16; xx++)
            if (rows[yy] & (1 << xx)) rect(s, x + xx, y + yy, 1, 1, color);
}
// 1-bit, most significant bit = leftmost pixel (Pillow mode "1").
static void blit1(uint16_t *s, int x, int y, const uint8_t *bits, int w, int h, uint16_t color) {
    int stride = w / 8;
    for (int yy = 0; yy < h; yy++)
        for (int xx = 0; xx < w; xx++)
            if (bits[yy * stride + xx / 8] & (0x80 >> (xx & 7)))
                if (x + xx < W && y + yy < H) s[(y + yy) * W + x + xx] = color;
}
// A server title bitmap holds two 12-pixel lines; centre a single line.
static int one_line(const uint8_t *bits) {
    for (int i = 12 * (UI_TITLE_W / 8); i < UI_TITLE_BYTES; i++) if (bits[i]) return 0;
    return 1;
}
// Wrap UTF-8 text onto up to `rows` lines of `width` pixels (breaks at spaces).
static void text_wrap(uint16_t *s, int x, int y, const char *t, int width, int rows, uint16_t color) {
    for (int row = 0; row < rows && *t; row++) {
        const char *nl = strchr(t, '\n');
        int limit = nl ? nl - t : (int)strlen(t);
        int n = fit_bytes(t, width);
        if (n > limit) n = limit;
        int brk = n;
        if (n < limit) {
            for (int i = n; i > 0; i--) if (t[i] == ' ') { brk = i; break; }
            if (brk == 0) brk = n;
        }
        char line[256];
        if (row == rows - 1 && n < limit) {
            int m = limit < 250 ? limit : 250;
            memcpy(line, t, m); line[m] = 0;
            text_fit(s, x, y + row * 12, line, width, color);
            return;
        }
        if (brk > 250) brk = 250;
        memcpy(line, t, brk); line[brk] = 0;
        text_at(s, x, y + row * 12, line, color, 1);
        t += brk;
        while (*t == ' ' || *t == '\n') t++;
    }
}

static const uint16_t icon_search[16] = {0,0,0x01f0,0x0208,0x0404,0x0404,0x0404,0x0404,0x0404,0x0208,0x05f0,0x0c00,0x1800,0x3000,0,0};
static const uint16_t icon_play_solid[16] = {0,0,0x0018,0x0078,0x01f8,0x07f8,0x1ff8,0x7ff8,0x1ff8,0x07f8,0x01f8,0x0078,0x0018,0,0,0};
static const uint16_t icon_pause[16] = {0,0,0,0x0e70,0x0e70,0x0e70,0x0e70,0x0e70,0x0e70,0x0e70,0x0e70,0x0e70,0x0e70,0,0,0};
static const uint16_t icon_back[16] = {0,0,0,0,0x00c0,0x0060,0x0030,0x1ff8,0x1ff8,0x0030,0x0060,0x00c0,0,0,0,0};
static const uint16_t icon_note[16] = {0,0,0x0fc0,0x0840,0x0fc0,0x0840,0x0840,0x0840,0x0840,0x0840,0x0c70,0x0e78,0x0670,0,0,0};
static const uint16_t icon_pc[16] = {0,0,0x3ffc,0x2004,0x2004,0x2004,0x2004,0x2004,0x3ffc,0x0080,0x0080,0x03e0,0,0,0,0};
static const uint16_t icon_chart[16] = {0,0,0x0004,0x0004,0x0204,0x0204,0x0224,0x0224,0x0224,0x2224,0x2224,0x2224,0x3ffc,0,0,0};

static void begin(void) { rect(paint, 0, 0, W, H, BG); }
static void publish(void) {
    DC_FlushRange(paint, sizeof(paint));
    dmaCopyHalfWords(3, paint, sub_vram, sizeof(paint));
}
volatile int ui_battery_wait;  // diagnostic: set while waiting for the ARM7's answer
static void battery_icon(uint16_t *s, int x, int y) {
    ui_battery_wait = 1;
    unsigned raw = getBatteryLevel() & BATTERY_LEVEL_MASK, level = (raw + 3) / 4;
    ui_battery_wait = 0;
    outline(s, x, y, 20, 9, MUTED); rect(s, x + 20, y + 2, 2, 5, MUTED);
    for (unsigned i = 0; i < level && i < 4; i++) rect(s, x + 2 + i * 4, y + 2, 3, 5, level <= 1 ? WARNING : INK);
}
static void radio_icon(uint16_t *s, int x, int y) {
    rect(s, x + 6, y + 4, 1, 6, INK); rect(s, x + 5, y + 3, 3, 2, INK);
    rect(s, x + 2, y + 1, 1, 4, INK); rect(s, x + 10, y + 1, 1, 4, INK);
    rect(s, x + 3, y, 7, 1, INK);
}
static void title_bar(const char *title, const char *right) {
    rect(paint, 0, 0, W, 16, CHROME); rect(paint, 0, 0, W, 1, PAPER);
    rect(paint, 0, 15, W, 1, RULE);
    // Small play-mark before the app title, echoing the top-screen logo.
    static const int disc[9] = {3, 7, 9, 9, 9, 9, 9, 7, 3};
    for (int i = 0; i < 9; i++) rect(paint, 7 + (9 - disc[i]) / 2, 3 + i, disc[i], 1, ACCENT);
    static const int tri[5] = {1, 2, 3, 2, 1};
    for (int i = 0; i < 5; i++) rect(paint, 10, 5 + i, tri[i], 1, WHITE);
    text_fit(paint, 21, 3, title, right ? 180 - text_width(right) : 185, INK);
    battery_icon(paint, 228, 4);
    radio_icon(paint, 212, 3);
    if (right) text_at(paint, 207 - text_width(right), 3, right, MUTED, 1);
}
static void footer(const char *line1, const char *line2) {
    rect(paint, 0, 163, W, 1, RULE);
    if (line1) text_at(paint, 7, 166, line1, MUTED, 1);
    if (line2) text_at(paint, 7, 178, line2, MUTED, 1);
}
// Raised key in the DSi Desk style; pressed keys sink by one pixel.
static void key(int x, int y, int w, int h, int pressed, int active) {
    rect(paint, x, y, w, h, pressed ? CHROME : PAPER);
    outline(paint, x, y, w, h, pressed || active ? ACCENT : RULE);
    if (pressed) outline(paint, x + 1, y + 1, w - 2, h - 2, ACCENT);
    else { rect(paint, x + 1, y + h - 2, w - 2, 1, CHROME); rect(paint, x + w - 2, y + 1, 1, h - 2, CHROME); }
}
static void icon_button(int x, int y, int w, int h, const uint16_t icon[16], const char *label,
                        int pressed, uint16_t color) {
    key(x, y, w, h, pressed, 0);
    int shift = pressed ? 1 : 0, lw = text_width(label);
    int total = 16 + 5 + lw;
    stamp(paint, x + (w - total) / 2 + shift, y + (h - 16) / 2 + shift, icon, color);
    text_at(paint, x + (w - total) / 2 + 21 + shift, y + (h - 10) / 2 + 1 + shift, label, color, 1);
}

void ui_init(void) {
    videoSetModeSub(MODE_5_2D);
    vramSetBankC(VRAM_C_SUB_BG);
    int bg = bgInitSub(2, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    sub_vram = bgGetGfxPtr(bg);
    begin(); publish();
}

// ---- Top screen while idle: animated character (art.c), name, PC address ---
extern const unsigned art_size, art_frames;
extern const uint16_t art_palette[256];
extern const uint8_t art_pixels[][156 * 156];
static int splash_active, splash_frame;
static unsigned splash_clock;
#define TOP ((uint16_t *)0x06000000)  // video page 0, shown while idle

static void splash_draw_frame(void) {
    const uint8_t *src = art_pixels[splash_frame];
    int x0 = (W - (int)art_size) / 2, y0 = 4;
    for (unsigned y = 0; y < art_size; y++) {
        uint16_t *row = TOP + (y0 + y) * W + x0;
        const uint8_t *in = src + y * art_size;
        for (unsigned x = 0; x < art_size; x++) row[x] = art_palette[in[x]];
    }
}
void ui_top_splash(const char *host, int port) {
    rect(TOP, 0, 0, W, H, BG);
    rect(TOP, 0, 163, W, 1, RULE);
    const char *name = "YouTube DSi HQ2";
    text_at(TOP, (W - text_width(name)) / 2, 167, name, INK, 1);
    char line[64];
    snprintf(line, sizeof(line), "Server %s:%d", host, port);
    text_at(TOP, (W - text_width(line)) / 2, 179, line, MUTED, 1);
    splash_frame = 0; splash_clock = 0; splash_active = 1;
    splash_draw_frame();
}
void ui_top_stop(void) { splash_active = 0; }
// Called once per VBlank from the menu loops: 9 frames at 100 ms (6 VBlanks).
void ui_top_tick(void) {
    if (!splash_active || ++splash_clock < 6) return;
    splash_clock = 0;
    splash_frame = (splash_frame + 1) % art_frames;
    splash_draw_frame();
}

void ui_status(const char *title, const char *line1, const char *line2, const char *hint) {
    begin();
    title_bar(title, NULL);
    rect(paint, 16, 58, 224, 52, PAPER); outline(paint, 16, 58, 224, 52, RULE);
    if (line1) text_center(paint, 128, line2 ? 70 : 78, line1, INK);
    if (line2) text_center(paint, 128, 86, line2, MUTED);
    footer(hint, NULL);
    publish();
}

void ui_crash(const char *const *lines, int count) {
    rect(paint, 0, 0, W, H, WARNING);
    for (int i = 0; i < count; i++) text_at(paint, 3, 2 + i * 11, lines[i], PAPER, 1);
    for (int i = 0; i < W * H; i++) sub_vram[i] = paint[i];
}

void ui_error(const char *message) {
    begin();
    title_bar("Something went wrong", NULL);
    rect(paint, 12, 36, 232, 100, PAPER); outline(paint, 12, 36, 232, 100, WARNING);
    rect(paint, 12, 36, 232, 3, WARNING);
    text_wrap(paint, 22, 50, message, 212, 6, INK);
    footer("A: OK", NULL);
    publish();
}

void ui_home(const char *host, int port, int pressed) {
    begin();
    title_bar("YouTube DSi", NULL);
    // Search field: the main touch target.
    key(8, 24, 240, 36, pressed == 1, 0);
    int shift = pressed == 1;
    stamp(paint, 18 + shift, 34 + shift, icon_search, MUTED);
    text_at(paint, 40 + shift, 37 + shift, "Search YouTube", MUTED, 1);
    icon_button(8, 70, 116, 40, icon_note, "Test", pressed == 2, INK);
    icon_button(132, 70, 116, 40, icon_pc, "Server", pressed == 3, INK);
    rect(paint, 8, 120, 240, 34, PAPER); outline(paint, 8, 120, 240, 34, RULE);
    char line[64];
    snprintf(line, sizeof(line), "Server  %s:%d", host, port);
    text_at(paint, 16, 125, line, INK, 1);
    text_at(paint, 16, 139, "Found automatically on the same Wi-Fi", MUTED, 1);
    footer("A: Search   X: Test   Y: Server", "START: Exit");
    publish();
}

int ui_home_hit(int x, int y) {
    if (x >= 8 && x < 248 && y >= 24 && y < 60) return 1;
    if (y >= 70 && y < 110) {
        if (x >= 8 && x < 124) return 2;
        if (x >= 132 && x < 248) return 3;
    }
    return 0;
}

#define ROW_Y 20
#define ROW_H 34
void ui_results(const char *query, int count, int selected, int pressed,
                const char (*titles)[101], const uint8_t *bitmaps) {
    begin();
    int pages = (count + UI_RESULTS_PER_PAGE - 1) / UI_RESULTS_PER_PAGE;
    int page = selected / UI_RESULTS_PER_PAGE;
    char right[24] = "";
    if (pages > 1) snprintf(right, sizeof(right), "%d / %d", page + 1, pages);
    char title[264];
    snprintf(title, sizeof(title), "\"%s\"", query);
    title_bar(title, right);
    if (!count) {
        rect(paint, 16, 58, 224, 40, PAPER); outline(paint, 16, 58, 224, 40, RULE);
        text_center(paint, 128, 73, "No results", INK);
    }
    for (int row = 0; row < UI_RESULTS_PER_PAGE; row++) {
        int i = page * UI_RESULTS_PER_PAGE + row;
        if (i >= count) break;
        int y = ROW_Y + row * (ROW_H + 1);
        int sel = i == selected, push = i == pressed;
        rect(paint, 4, y, 248, ROW_H, push ? CHROME : PAPER);
        outline(paint, 4, y, 248, ROW_H, sel ? ACCENT : RULE);
        if (sel) { outline(paint, 5, y + 1, 246, ROW_H - 2, ACCENT); rect(paint, 4, y, 4, ROW_H, ACCENT); }
        if (bitmaps) {
            const uint8_t *b = bitmaps + i * UI_TITLE_BYTES;
            blit1(paint, 14, y + (one_line(b) ? 12 : 6), b, UI_TITLE_W, UI_TITLE_H, INK);
        }
        else text_wrap(paint, 14, y + 5, titles[i], 232, 2, INK);
    }
    footer("A: Play   B: Back   Up/Down: Select", pages > 1 ? "L / R: Page   Tap a title to play" : "Tap a title to play");
    publish();
}

int ui_results_hit(int x, int y, int selected, int count) {
    if (x < 4 || x >= 252 || y < ROW_Y) return -1;
    int row = (y - ROW_Y) / (ROW_H + 1);
    if (row >= UI_RESULTS_PER_PAGE) return -1;
    int i = selected / UI_RESULTS_PER_PAGE * UI_RESULTS_PER_PAGE + row;
    return i < count ? i : -1;
}

// ---- Hangul input (dubeolsik / 2-set) -------------------------------------
// The field is kept as a sequence of raw keystrokes: ASCII characters or single
// compatibility jamo (U+3131..U+3163). Syllables are composed from that sequence
// every time it changes, so backspace removes one keystroke (one jamo).
#define MAX_KEYS 160
static uint16_t strokes[MAX_KEYS];
static int stroke_count;

// Choseong / jongseong order expressed as compatibility jamo offsets from U+3131.
static const int8_t cho_index[30] = {0,1,-1,2,-1,-1,3,4,5,-1,-1,-1,-1,-1,-1,-1,6,7,8,-1,9,10,11,12,13,14,15,16,17,18};
static const int8_t jong_index[30] = {1,2,3,4,5,6,7,-1,8,9,10,11,12,13,14,15,16,17,-1,18,19,20,21,22,-1,23,24,25,26,27};
static int is_jamo(unsigned c) { return c >= 0x3131 && c <= 0x3163; }
static int is_vowel(unsigned c) { return c >= 0x314F && c <= 0x3163; }
static unsigned vowel_pair(unsigned a, unsigned b) {
    static const uint16_t t[7][3] = {{0x3157,0x314F,0x3158},{0x3157,0x3150,0x3159},{0x3157,0x3163,0x315A},
        {0x315C,0x3153,0x315D},{0x315C,0x3154,0x315E},{0x315C,0x3163,0x315F},{0x3161,0x3163,0x3162}};
    for (int i = 0; i < 7; i++) if (t[i][0] == a && t[i][1] == b) return t[i][2];
    return 0;
}
static unsigned final_pair(unsigned a, unsigned b) {
    static const uint16_t t[11][3] = {{0x3131,0x3145,0x3133},{0x3134,0x3148,0x3135},{0x3134,0x314E,0x3136},
        {0x3139,0x3131,0x313A},{0x3139,0x3141,0x313B},{0x3139,0x3142,0x313C},{0x3139,0x3145,0x313D},
        {0x3139,0x314C,0x313E},{0x3139,0x314D,0x313F},{0x3139,0x314E,0x3140},{0x3142,0x3145,0x3144}};
    for (int i = 0; i < 11; i++) if (t[i][0] == a && t[i][1] == b) return t[i][2];
    return 0;
}
static void split_pair(unsigned c, uint16_t *out, int *n) {
    static const uint16_t v[7][3] = {{0x3158,0x3157,0x314F},{0x3159,0x3157,0x3150},{0x315A,0x3157,0x3163},
        {0x315D,0x315C,0x3153},{0x315E,0x315C,0x3154},{0x315F,0x315C,0x3163},{0x3162,0x3161,0x3163}};
    static const uint16_t f[11][3] = {{0x3133,0x3131,0x3145},{0x3135,0x3134,0x3148},{0x3136,0x3134,0x314E},
        {0x313A,0x3139,0x3131},{0x313B,0x3139,0x3141},{0x313C,0x3139,0x3142},{0x313D,0x3139,0x3145},
        {0x313E,0x3139,0x314C},{0x313F,0x3139,0x314D},{0x3140,0x3139,0x314E},{0x3144,0x3142,0x3145}};
    for (int i = 0; i < 7; i++) if (v[i][0] == c) { out[(*n)++] = v[i][1]; out[(*n)++] = v[i][2]; return; }
    for (int i = 0; i < 11; i++) if (f[i][0] == c) { out[(*n)++] = f[i][1]; out[(*n)++] = f[i][2]; return; }
    out[(*n)++] = c;
}

static int put_utf8(char *out, int at, int size, unsigned cp) {
    char b[4]; int n;
    if (cp < 0x80) { b[0] = cp; n = 1; }
    else if (cp < 0x800) { b[0] = 0xc0 | (cp >> 6); b[1] = 0x80 | (cp & 0x3f); n = 2; }
    else { b[0] = 0xe0 | (cp >> 12); b[1] = 0x80 | ((cp >> 6) & 0x3f); b[2] = 0x80 | (cp & 0x3f); n = 3; }
    if (at + n >= size) return -1;
    memcpy(out + at, b, n);
    return at + n;
}
// Compose strokes into UTF-8. Returns byte length, or -1 if it does not fit.
static int compose(char *out, int size) {
    int at = 0;
    unsigned cho = 0, v[2], f[2];
    int nv = 0, nf = 0;
#define EMIT(cp) do { at = put_utf8(out, at, size, (cp)); if (at < 0) return -1; } while (0)
#define FLUSH() do { \
        if (cho && nv) { \
            unsigned vv = nv == 2 ? vowel_pair(v[0], v[1]) : v[0]; \
            unsigned ff = nf == 2 ? final_pair(f[0], f[1]) : nf ? f[0] : 0; \
            EMIT(0xAC00 + (cho_index[cho - 0x3131] * 21 + (vv - 0x314F)) * 28 + \
                 (ff ? jong_index[ff - 0x3131] : 0)); \
        } else if (cho) EMIT(cho); \
        else if (nv) EMIT(nv == 2 ? vowel_pair(v[0], v[1]) : v[0]); \
        cho = 0; nv = nf = 0; } while (0)
    for (int i = 0; i < stroke_count; i++) {
        unsigned t = strokes[i];
        if (!is_jamo(t)) { FLUSH(); EMIT(t); continue; }
        if (!is_vowel(t)) {
            if (!nv) { if (cho) FLUSH(); cho = t; }
            else if (!cho) { FLUSH(); cho = t; }
            else if (!nf && jong_index[t - 0x3131] > 0) { f[0] = t; nf = 1; }
            else if (nf == 1 && final_pair(f[0], t)) { f[1] = t; nf = 2; }
            else { FLUSH(); cho = t; }
        } else {
            if (nf) { unsigned moved = f[--nf]; FLUSH(); cho = moved; v[0] = t; nv = 1; }
            else if (!nv) { v[0] = t; nv = 1; }
            else if (nv == 1 && vowel_pair(v[0], t)) { v[1] = t; nv = 2; }
            else { FLUSH(); v[0] = t; nv = 1; }
        }
    }
    FLUSH();
#undef FLUSH
#undef EMIT
    out[at] = 0;
    return at;
}
// Turn existing UTF-8 text back into keystrokes, so it can be edited jamo by jamo.
static void strokes_from_utf8(const char *t) {
    static const uint16_t cho_cp[19] = {0x3131,0x3132,0x3134,0x3137,0x3138,0x3139,0x3141,0x3142,0x3143,0x3145,
                                        0x3146,0x3147,0x3148,0x3149,0x314A,0x314B,0x314C,0x314D,0x314E};
    static const uint16_t jong_cp[28] = {0,0x3131,0x3132,0x3133,0x3134,0x3135,0x3136,0x3137,0x3139,0x313A,0x313B,
        0x313C,0x313D,0x313E,0x313F,0x3140,0x3141,0x3142,0x3144,0x3145,0x3146,0x3147,0x3148,0x314A,0x314B,
        0x314C,0x314D,0x314E};
    stroke_count = 0;
    while (*t && stroke_count < MAX_KEYS - 6) {
        unsigned cp = next_cp(&t);
        if (cp >= 0xAC00 && cp <= 0xD7A3) {
            unsigned i = cp - 0xAC00;
            strokes[stroke_count++] = cho_cp[i / 588];
            split_pair(0x314F + (i % 588) / 28, strokes, &stroke_count);
            if (i % 28) split_pair(jong_cp[i % 28], strokes, &stroke_count);
        } else if (is_jamo(cp)) split_pair(cp, strokes, &stroke_count);
        else if (cp >= 32 && cp < 127) strokes[stroke_count++] = cp;
    }
}

// ---- Touch keyboard -------------------------------------------------------
enum { K_SHIFT = 1, K_BACK = 2, K_SPACE = 3, K_OK = 4, K_LANG = 5 };
typedef struct { int x, y, w, h; int code; } Key;
static Key keys[64];
static int key_count;
static const char *const rows[3] = {"qwertyuiop", "asdfghjkl'", "zxcvbnm."};
// Dubeolsik jamo for each latin key (a..z), plain and with shift.
static const uint16_t ko_plain[26] = {0x3141,0x3160,0x314A,0x3147,0x3137,0x3139,0x314E,0x3157,0x3151,0x3153,
    0x314F,0x3163,0x3161,0x315C,0x3150,0x3154,0x3142,0x3131,0x3134,0x3145,0x3155,0x314D,0x3148,0x314C,0x315B,0x314B};
static unsigned ko_key(int c, int shift) {
    unsigned j = ko_plain[c - 'a'];
    if (shift) switch (j) {
        case 0x3142: return 0x3143; case 0x3148: return 0x3149; case 0x3137: return 0x3138;
        case 0x3131: return 0x3132; case 0x3145: return 0x3146; case 0x3150: return 0x3152;
        case 0x3154: return 0x3156;
    }
    return j;
}

static void add_key(int x, int y, int w, int code) {
    keys[key_count++] = (Key){x, y, w, 21, code};
}
static void layout_keys(int hangul_allowed) {
    key_count = 0;
    const int pitch = 23, y0 = 46;
    const char *digits = "1234567890-";
    for (int i = 0; i < 11; i++) add_key(2 + i * pitch, y0, 22, digits[i]);
    for (int r = 0; r < 2; r++)
        for (int i = 0; i < 10; i++) add_key(13 + i * pitch, y0 + (r + 1) * pitch, 22, rows[r][i]);
    int y = y0 + 3 * pitch;
    add_key(2, y, 33, -K_SHIFT);
    for (int i = 0; i < 8; i++) add_key(37 + i * pitch, y, 22, rows[2][i]);
    add_key(221, y, 33, -K_BACK);
    y += pitch;
    if (hangul_allowed) {
        add_key(2, y, 44, -K_LANG);
        add_key(48, y, 136, -K_SPACE);
    } else add_key(2, y, 182, -K_SPACE);
    add_key(186, y, 68, -K_OK);
}
static void draw_keyboard(const char *label, const char *text, int shift, int korean, int pressed) {
    begin();
    title_bar(label, NULL);
    rect(paint, 6, 21, 244, 20, PAPER); outline(paint, 6, 21, 244, 20, ACCENT);
    const char *shown = text;
    while (text_width(shown) > 230) next_cp(&shown);
    text_at(paint, 12, 26, shown, INK, 1);
    rect(paint, 12 + text_width(shown) + 1, 25, 1, 12, ACCENT);
    for (int i = 0; i < key_count; i++) {
        Key *k = &keys[i];
        int active = (k->code == -K_SHIFT && shift) || (k->code == -K_LANG && korean);
        key(k->x, k->y, k->w, k->h, i == pressed, active);
        int d = i == pressed;
        char cap[12] = "";
        uint16_t color = INK;
        if (k->code >= 'a' && k->code <= 'z' && korean) {
            int n = put_utf8(cap, 0, sizeof(cap), ko_key(k->code, shift));
            cap[n > 0 ? n : 0] = 0;
        }
        else if (k->code > 0) {
            cap[0] = shift && k->code >= 'a' && k->code <= 'z' ? k->code - 32 : k->code; cap[1] = 0;
        }
        else if (k->code == -K_SHIFT) snprintf(cap, sizeof(cap), "Shift");
        else if (k->code == -K_BACK) snprintf(cap, sizeof(cap), "Del");
        else if (k->code == -K_SPACE) { snprintf(cap, sizeof(cap), "space"); color = MUTED; }
        else if (k->code == -K_OK) { snprintf(cap, sizeof(cap), "OK"); color = ACCENT; }
        else if (k->code == -K_LANG) snprintf(cap, sizeof(cap), korean ? "\xed\x95\x9c" : "A");
        text_at(paint, k->x + (k->w - text_width(cap)) / 2 + d, k->y + 6 + d, cap, color, 1);
    }
    footer(korean ? "START: OK   B: Cancel   SELECT: Clear   L: \xed\x95\x9c/A"
                  : "START: OK   B: Cancel   SELECT: Clear", NULL);
    publish();
}

int ui_keyboard(const char *label, char *text, unsigned size, void (*tick)(void), int hangul_allowed) {
    layout_keys(hangul_allowed);
    strokes_from_utf8(text);
    static int korean_mode = 1;
    int korean = hangul_allowed && korean_mode;
    int shift = 0, pressed = -1, dirty = 1, repeat = 0;
    char composed[256];
    compose(composed, sizeof(composed));
    while (1) {
        if (dirty) { draw_keyboard(label, composed, shift, korean, pressed); dirty = 0; }
        tick();
        unsigned down = keysDown(), held = keysHeld();
        if (down & KEY_B) return 0;
        if ((down & KEY_START) && composed[0]) { snprintf(text, size, "%s", composed); return 1; }
        if (down & KEY_SELECT) { stroke_count = 0; composed[0] = 0; dirty = 1; }
        if ((down & KEY_L) && hangul_allowed) { korean = korean_mode = !korean; shift = 0; dirty = 1; }
        int hit = -1;
        if (down & KEY_TOUCH) {
            touchPosition t;
            touchRead(&t);
            for (int i = 0; i < key_count; i++) {
                Key *k = &keys[i];
                if (t.px >= k->x && t.px < k->x + k->w && t.py >= k->y && t.py < k->y + k->h) { hit = i; break; }
            }
            if (hit >= 0) { pressed = hit; repeat = 0; dirty = 1; }
        } else if (pressed >= 0 && (held & KEY_TOUCH) && keys[pressed].code == -K_BACK) {
            // Hold Del: first repeat after 0.4 s, then 15 deletions per second.
            if (++repeat >= 24 && (repeat - 24) % 4 == 0) hit = pressed;
        }
        if (hit >= 0) {
            Key *k = &keys[hit];
            if (k->code == -K_OK) {
                if (!composed[0]) continue;
                draw_keyboard(label, composed, shift, korean, pressed);
                snprintf(text, size, "%s", composed);
                return 1;
            }
            if (k->code == -K_SHIFT) shift = !shift;
            else if (k->code == -K_LANG) { korean = korean_mode = !korean; shift = 0; }
            else if (k->code == -K_BACK) { if (stroke_count) stroke_count--; }
            else if (stroke_count < MAX_KEYS) {
                unsigned ch = k->code == -K_SPACE ? ' ' : (unsigned)k->code;
                if (ch >= 'a' && ch <= 'z') {
                    if (korean) ch = ko_key(ch, shift);
                    else if (shift) ch -= 32;
                    shift = 0;
                }
                strokes[stroke_count++] = ch;
                if (compose(composed, size) < 0) stroke_count--;  // would not fit
            }
            compose(composed, sizeof(composed));
            dirty = 1;
        }
        if (pressed >= 0 && !(held & KEY_TOUCH)) { pressed = -1; dirty = 1; }
    }
}

// ---- Player ---------------------------------------------------------------
void ui_player(const PlayerView *v) {
    begin();
    const char *state = v->mode == 0 ? "Loading" : v->mode == 2 ? "Buffering" : v->mode == 3 ? "Paused" : "Playing";
    title_bar("Now Playing", state);
    rect(paint, 4, 21, 248, 32, PAPER); outline(paint, 4, 21, 248, 32, RULE);
    if (v->title_bitmap)
        blit1(paint, 12, one_line(v->title_bitmap) ? 32 : 26, v->title_bitmap, UI_TITLE_W, UI_TITLE_H, INK);
    else if (v->title_text) text_wrap(paint, 12, 26, v->title_text, 232, 2, INK);

    char line[96], total[16];
    unsigned shown = v->seek_target >= 0 ? (unsigned)v->seek_target : v->seconds;
    snprintf(line, sizeof(line), "%u:%02u", shown / 60, shown % 60);
    if (v->duration > 0) {
        snprintf(total, sizeof(total), " / %d:%02d", v->duration / 60, v->duration % 60);
        strncat(line, total, sizeof(line) - strlen(line) - 1);
    }
    text_at(paint, 8, 66, line, v->seek_target >= 0 ? ACCENT : INK, 1);
    snprintf(line, sizeof(line), "Buffer %u.%us", v->buffer_ms / 1000, (v->buffer_ms % 1000) / 100);
    text_at(paint, 248 - text_width(line), 66, line, MUTED, 1);
    if (v->volume >= 0) {
        // DSi speaker volume (hardware buttons), centered between time and buffer.
        if (v->volume == 0) snprintf(line, sizeof(line), "Volume: mute");
        else snprintf(line, sizeof(line), "Volume %d%%", (v->volume * 100 + 15) / 31);
        text_at(paint, (256 - text_width(line)) / 2, 66, line, v->volume ? INK : WARNING, 1);
    }
    if (v->duration > 0) {
        // Position in the video; while choosing a seek, the target is marked.
        rect(paint, 8, 82, 240, 6, CHROME); outline(paint, 8, 82, 240, 6, RULE);
        unsigned at = v->seconds > (unsigned)v->duration ? (unsigned)v->duration : v->seconds;
        rect(paint, 9, 83, (int)(at * 238 / (unsigned)v->duration), 4, v->mode == 2 ? WARNING : ACCENT);
        if (v->seek_target >= 0) {
            int x = 8 + v->seek_target * 239 / v->duration;
            rect(paint, x - 1, 79, 3, 12, INK);
        }
    } else {
        // Buffer gauge: 0..8 s, with the 2.5 s resume point marked.
        rect(paint, 8, 82, 240, 6, CHROME); outline(paint, 8, 82, 240, 6, RULE);
        unsigned fill = v->buffer_ms > 8000 ? 8000 : v->buffer_ms;
        rect(paint, 9, 83, (int)(fill * 238 / 8000), 4, v->mode == 2 ? WARNING : ACCENT);
        rect(paint, 8 + 2500 * 240 / 8000, 80, 1, 10, MUTED);
    }

    icon_button(4, 96, 122, 40, v->mode == 3 ? icon_play_solid : icon_pause,
                v->mode == 3 ? "Resume" : "Pause", v->pressed == 1, INK);
    icon_button(130, 96, 122, 40, icon_back, "Back", v->pressed == 2, INK);

    if (v->show_stats) {
        snprintf(line, sizeof(line), "Picture %u fps   Quality %u   Wi-Fi %u KB/s",
                 v->picture_fps, v->quality, v->net_kib);
        text_at(paint, 7, 141, line, MUTED, 1);
        snprintf(line, sizeof(line), "Decode %u ms  Late %u  Wait %u  Gaps %u  Stack %uK",
                 v->decode_ms, v->late, v->rebuffers, v->gaps, v->stack_kib);
        text_at(paint, 7, 152, line, v->late || v->gaps ? WARNING : MUTED, 1);
    } else {
        stamp(paint, 6, 142, icon_chart, MUTED);
        text_at(paint, 25, 145, "Tap here for stream details", MUTED, 1);
    }
    footer(v->seek_target >= 0 ? "A: Jump here   B: Cancel" :
           v->duration > 0 ? "A: Pause   </> or touch bar: Seek" : "A: Pause / Resume   B: Back",
           v->seek_target >= 0 || v->duration <= 0 ? NULL : "B: Back");
    publish();
}

int ui_player_hit(int x, int y) {
    if (y >= 96 && y < 136) {
        if (x >= 4 && x < 126) return 1;
        if (x >= 130 && x < 252) return 2;
    }
    if (y >= 138 && y < 163) return 3;
    if (y >= 72 && y < 96) return 4;
    return 0;
}

int ui_player_bar_seconds(int x, int duration) {
    if (x < 8) x = 8;
    if (x > 248) x = 248;
    int at = (x - 8) * duration / 240;
    return at > duration - 2 ? (duration > 2 ? duration - 2 : 0) : at;
}
