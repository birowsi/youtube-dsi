#ifndef HQ_PLAYER_H
#define HQ_PLAYER_H
#include <stdint.h>
void hq_video_init(void);
void hq_video_reset(void);
// Lid closed: both backlights off, Wi-Fi and audio keep running. Call after scanKeys().
int hq_lid_update(void);
// Crash screen that never dereferences the crashed stack; shows memory around r0/r4.
void hq_crash_handler(void);
// start/duration in seconds (duration 0: unknown, no seeking). Returns 1 with
// *seek_to >= 0 when the user picked a new position with Left/Right.
int hq_playback(int fd, char *error, unsigned error_size,
                const uint8_t *title_bitmap, const char *title_text,
                int start, int duration, int *seek_to);
#endif
