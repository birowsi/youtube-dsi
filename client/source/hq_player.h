#ifndef HQ_PLAYER_H
#define HQ_PLAYER_H
void hq_video_init(void);
void hq_video_reset(void);
int hq_playback(int fd, char *error, unsigned error_size);
#endif
