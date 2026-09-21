/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0. */
#include "backtrack/bt_peaks.h"

#include <stdlib.h>
#include <string.h>

/* A bucket that covers no frames has no waveform in it. Reporting 0/0 - a
 * flat line - is the honest answer and draws correctly; reporting -1/+1 or
 * leaving it uninitialised both draw a lie. */
static void empty_bucket(float *lo, float *hi) { *lo = 0.0f; *hi = 0.0f; }

static void scan(const float *const *pcm, int32_t channels,
                 bt_frame frames, bt_frame a, bt_frame b,
                 float *lo_out, float *hi_out) {
    if (a < 0) a = 0;
    if (b > frames) b = frames;
    if (b <= a) { empty_bucket(lo_out, hi_out); return; }

    float lo =  1.0f, hi = -1.0f;
    for (int32_t c = 0; c < channels; c++) {
        const float *s = pcm[c];
        for (bt_frame i = a; i < b; i++) {
            float v = s[i];
            if (v < lo) lo = v;
            if (v > hi) hi = v;
        }
    }
    /* Possible only if a >= b, which is handled above; kept as a guard so a
     * future change cannot silently emit an inverted range. */
    if (hi < lo) { empty_bucket(lo_out, hi_out); return; }
    *lo_out = lo;
    *hi_out = hi;
}

bt_err bt_peaks_build(const float *const *pcm, int32_t channels,
                      bt_frame frames, bt_frame frames_per_bucket,
                      bt_peaks *out) {
    if (!pcm || !out || channels <= 0 || frames <= 0 || frames_per_bucket <= 0)
        return BT_ERR_RANGE;

    memset(out, 0, sizeof(*out));

    /* Round up: the tail of a stem is part of the stem. */
    int64_t n = (frames + frames_per_bucket - 1) / frames_per_bucket;
    if (n <= 0 || n > 1 << 26) return BT_ERR_RANGE;

    out->min = (float *)malloc((size_t)n * sizeof(float));
    out->max = (float *)malloc((size_t)n * sizeof(float));
    if (!out->min || !out->max) {
        free(out->min); free(out->max);
        memset(out, 0, sizeof(*out));
        return BT_ERR_ALLOC;
    }

    for (int64_t k = 0; k < n; k++)
        scan(pcm, channels, frames,
             k * frames_per_bucket, (k + 1) * frames_per_bucket,
             &out->min[k], &out->max[k]);

    out->buckets           = (int32_t)n;
    out->frames_per_bucket = frames_per_bucket;
    out->frames            = frames;
    out->channels          = channels;
    return BT_OK;
}

void bt_peaks_free(bt_peaks *p) {
    if (!p) return;
    free(p->min);
    free(p->max);
    memset(p, 0, sizeof(*p));
}

bt_err bt_peaks_range(const float *const *pcm, int32_t channels,
                      bt_frame frames, bt_frame from, bt_frame to,
                      float *min_out, float *max_out, int32_t buckets) {
    if (!pcm || !min_out || !max_out || channels <= 0 || buckets <= 0)
        return BT_ERR_RANGE;
    if (to <= from) return BT_ERR_RANGE;

    const bt_frame span = to - from;
    for (int32_t k = 0; k < buckets; k++) {
        /* Computed from k rather than accumulated, so the last bucket ends
         * exactly at `to` however the division falls - the same discipline
         * beat positions use. */
        bt_frame a = from + (bt_frame)((double)span * k / buckets);
        bt_frame b = from + (bt_frame)((double)span * (k + 1) / buckets);
        if (b <= a) b = a + 1;
        scan(pcm, channels, frames, a, b, &min_out[k], &max_out[k]);
    }
    return BT_OK;
}

bt_err bt_peaks_read(const bt_peaks *p, bt_frame from, bt_frame to,
                     float *min_out, float *max_out, int32_t buckets) {
    if (!p || !p->min || !min_out || !max_out || buckets <= 0) return BT_ERR_RANGE;
    if (to <= from) return BT_ERR_RANGE;

    const bt_frame span = to - from;
    for (int32_t k = 0; k < buckets; k++) {
        bt_frame a = from + (bt_frame)((double)span * k / buckets);
        bt_frame b = from + (bt_frame)((double)span * (k + 1) / buckets);
        if (b <= a) b = a + 1;

        int64_t ba = a / p->frames_per_bucket;
        int64_t bb = (b + p->frames_per_bucket - 1) / p->frames_per_bucket;
        if (ba < 0) ba = 0;
        if (bb > p->buckets) bb = p->buckets;

        if (bb <= ba) { empty_bucket(&min_out[k], &max_out[k]); continue; }

        float lo = p->min[ba], hi = p->max[ba];
        for (int64_t i = ba + 1; i < bb; i++) {
            if (p->min[i] < lo) lo = p->min[i];
            if (p->max[i] > hi) hi = p->max[i];
        }
        min_out[k] = lo;
        max_out[k] = hi;
    }
    return BT_OK;
}
