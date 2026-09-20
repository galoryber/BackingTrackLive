/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * Minimal RIFF/WAVE reader and writer. Decodes to planar float32.
 * Supports PCM 8/16/24/32-bit integer and 32/64-bit IEEE float, plus
 * WAVE_FORMAT_EXTENSIBLE. Untrusted input; covered by fuzz/fuzz_wav.c.
 *
 * Callers generally want bt_audio_decode_file(), which dispatches across all
 * supported formats. This header is the WAV backend plus the writer that the
 * offline renderer and the golden tests use. Free results with
 * bt_audio_free().
 */
#ifndef BT_WAV_H
#define BT_WAV_H

#include "bt_types.h"
#include "bt_error.h"
#include "bt_audio.h"

#ifdef __cplusplus
extern "C" {
#endif

bt_err bt_wav_decode(const void *data, size_t len, bt_audio *out);
bt_err bt_wav_read_file(const char *path, bt_audio *out);

/* Writes 24-bit PCM. Used by the offline renderer and the golden tests. */
bt_err bt_wav_write_file(const char *path, const float *const *pcm,
                         int32_t channels, int32_t sample_rate, bt_frame frames);

#ifdef __cplusplus
}
#endif
#endif /* BT_WAV_H */
