#ifndef HQ_PLAYER_H
#define HQ_PLAYER_H
#include <stdint.h>
void hq_video_init(void);
void hq_video_reset(void);
// Lid closed: both backlights off, Wi-Fi and audio keep running. Call after scanKeys().
int hq_lid_update(void);
// Fetches the ARM7's cached DSi volume/battery without waiting; call every frame.
void hq_status_poll(void);
extern u32 hq_battery_raw;   // getBatteryLevel() value from the ARM7, 0xFFFFFFFF unknown
// Crash screen that never dereferences the crashed stack; shows memory around r0/r4.
void hq_crash_handler(void);
// Threads created through cothread_create (net_stacks.c): count and deepest stack use.
int hq_net_stack_count(void);
unsigned hq_net_stack_peak(int index);
cothread_t hq_thread_id(int index);
// start/duration in seconds (duration 0: unknown, no seeking). Returns 1 with
// *seek_to >= 0 when the user picked a new position on the bar. `resumed` marks a
// stream reopened by a seek: the last picture stays up and playback starts sooner.
int hq_playback(int fd, char *error, unsigned error_size,
                const uint8_t *title_bitmap, const char *title_text,
                int start, int duration, int *seek_to, int resumed);
#endif
