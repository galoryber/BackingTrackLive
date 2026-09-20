/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * Decoded audio, and the one entry point that turns a file of any supported
 * format into it.
 *
 * Everything downstream - the resampler, the mixer, the engine - works on
 * planar float32. Decoding happens exactly once, at load, and by the time the
 * engine sees a stem the format it arrived in has stopped mattering.
 */
#ifndef BT_AUDIO_H
#define BT_AUDIO_H

#include "bt_types.h"
#include "bt_error.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float  **pcm;          /* planar: pcm[channel][frame] */
    int32_t  channels;
    int32_t  sample_rate;
    bt_frame frames;
} bt_audio;

/* Format is detected from the file's contents, not its extension - a stem
 * named .wav that is really an MP3 is a thing that happens when people
 * re-save files, and it should just work. The extension is only a fallback
 * for MP3, which has no reliable magic number. */
bt_err bt_audio_decode_mem(const void *data, size_t len, const char *hint_ext,
                           bt_audio *out);
bt_err bt_audio_decode_file(const char *path, bt_audio *out);
void   bt_audio_free(bt_audio *a);

/* Human-readable list of what decodes, for help text and error messages. */
const char *bt_audio_formats(void);

#ifdef __cplusplus
}
#endif
#endif /* BT_AUDIO_H */
