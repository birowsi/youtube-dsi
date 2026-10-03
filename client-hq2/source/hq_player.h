#ifndef HQ_PLAYER_H
#define HQ_PLAYER_H
#include <stdint.h>
void hq_video_init(void);
void hq_video_reset(void);
// Lid closed: both backlights off, Wi-Fi and audio keep running. Call after scanKeys().
int hq_lid_update(void);
// Fetches the ARM7's cached DSi volume/battery without waiting; call every frame.
void hq_status_poll(void);
// Watchdog phases for the caller's part of leaving playback; hq_watch_end() after close().
void hq_watch_phase(unsigned phase);
void hq_watch_end(void);
// Auto-resume: when set, a stream that gets no data for 8 s returns with *seek_to at
// the current position and hq_resumed_after_stall=1, so the caller reconnects.
extern int hq_auto_resume, hq_resumed_after_stall;
extern u32 hq_battery_raw;   // getBatteryLevel() value from the ARM7, 0xFFFFFFFF unknown
// Crash screen that never dereferences the crashed stack; shows memory around r0/r4.
void hq_crash_handler(void);
// Threads created through cothread_create (net_stacks.c): count and deepest stack use.
int hq_net_stack_count(void);
unsigned hq_net_stack_peak(int index);
cothread_t hq_thread_id(int index);
// Canary after DSWiFi's shared struct (net_stacks.c).
unsigned hq_canary_check(unsigned *first, uint32_t *value);
uintptr_t hq_canary_owner(void);
// start/duration in seconds (duration 0: unknown, no seeking). Returns 1 with
// *seek_to >= 0 when the user picked a new position on the bar. `resumed` marks a
// stream reopened by a seek: the last picture stays up and playback starts sooner.
int hq_playback(int fd, char *error, unsigned error_size,
                const uint8_t *title_bitmap, const char *title_text,
                int start, int duration, int *seek_to, int resumed);
#endif
