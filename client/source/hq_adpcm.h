#ifndef HQ_ADPCM_H
#define HQ_ADPCM_H
#include <stdint.h>
#include <stddef.h>
// Decode the independent stereo IMA block; returns frame count, or -1.
int hq_decode_ima(const uint8_t *input, size_t bytes, int16_t *output, size_t capacity);
#endif
