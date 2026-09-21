/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * Waveform envelopes, for drawing a stem against a click grid.
 *
 * Dragging a stem into time is the one editing job that has to be precise,
 * and doing it by eye needs a waveform. Scanning five minutes of audio every
 * frame is not an option at sixty frames a second, so this builds a
 * downsampled min/max envelope once - the shape of the waveform at a
 * resolution the screen can actually show - and answers zoomed-in views
 * directly from the PCM, where the range is small enough that scanning it is
 * free.
 *
 * min/max per bucket, rather than RMS or an average: a single-sample transient
 * is exactly what you are looking for when aligning a drum stem, and
 * averaging is precisely the operation that hides it.
 */
#ifndef BT_PEAKS_H
#define BT_PEAKS_H

#include "bt_types.h"
#include "bt_error.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float   *min;                /* [buckets], -1..1 */
    float   *max;                /* [buckets], -1..1 */
    int32_t  buckets;
    bt_frame frames_per_bucket;
    bt_frame frames;             /* length of the source */
    int32_t  channels;           /* of the source; the envelope is one lane */
} bt_peaks;

/* Builds the cached envelope. `frames_per_bucket` trades memory for detail;
 * 512 is a reasonable overview for a five-minute stem (about 28k buckets,
 * a quarter of a megabyte).
 *
 * Channels are combined by taking the extremes across them, not by summing:
 * summing lets a stereo stem with out-of-phase content read as silence, which
 * is a lie about exactly the thing being looked for. */
bt_err bt_peaks_build(const float *const *pcm, int32_t channels,
                      bt_frame frames, bt_frame frames_per_bucket,
                      bt_peaks *out);
void   bt_peaks_free(bt_peaks *p);

/* Envelope of [from, to) into caller-supplied arrays of `buckets` entries.
 *
 * Use this when zoomed in past the cache's resolution. Out-of-range frames
 * read as silence rather than being an error, so a view that extends past
 * either end of the stem - which is normal when a stem is nudged - simply
 * shows nothing there. */
bt_err bt_peaks_range(const float *const *pcm, int32_t channels,
                      bt_frame frames, bt_frame from, bt_frame to,
                      float *min_out, float *max_out, int32_t buckets);

/* Reads the cached envelope over [from, to) instead of the PCM. Same shape of
 * answer, cheap at any zoom, coarse below the cache's resolution. */
bt_err bt_peaks_read(const bt_peaks *p, bt_frame from, bt_frame to,
                     float *min_out, float *max_out, int32_t buckets);

#ifdef __cplusplus
}
#endif
#endif /* BT_PEAKS_H */
