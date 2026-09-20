/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * Minimal RIFF/WAVE reader and writer. Decodes to planar float32.
 * Supports PCM 8/16/24/32-bit integer and 32/64-bit IEEE float, plus
 * WAVE_FORMAT_EXTENSIBLE. Untrusted input; covered by fuzz/fuzz_wav.c.
 */
#ifndef BT_WAV_H
#define BT_WAV_H

#include "bt_types.h"
#include "bt_error.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float  **pcm;         /* planar: pcm[ch][frame] */
    int32_t  channels;
    int32_t  sample_rate;
    bt_frame frames;
} bt_wav;

bt_err bt_wav_decode(const void *data, size_t len, bt_wav *out);
bt_err bt_wav_read_file(const char *path, bt_wav *out);
void   bt_wav_free(bt_wav *w);

/* Writes 24-bit PCM. Used by the offline renderer and the golden tests. */
bt_err bt_wav_write_file(const char *path, const float *const *pcm,
                         int32_t channels, int32_t sample_rate, bt_frame frames);

#ifdef __cplusplus
}
#endif
#endif /* BT_WAV_H */
