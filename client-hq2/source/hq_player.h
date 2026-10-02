#ifndef HQ_PLAYER_H
#define HQ_PLAYER_H
#include <stdint.h>
void hq_video_init(void);
void hq_video_reset(void);
// Lid closed: both backlights off, Wi-Fi and audio keep running. Call after scanKeys().
int hq_lid_update(void);
int hq_playback(int fd, char *error, unsigned error_size,
                const uint8_t *title_bitmap, const char *title_text);
#endif
