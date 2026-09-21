/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * Waveform envelopes. The thing that matters is that a transient survives:
 * a single-sample spike is exactly what you look for when dragging a drum
 * stem into time, and any averaging hides it.
 */
#include "bt_test.h"
#include "backtrack/bt_peaks.h"

static float *ramp_alloc(bt_frame n, float v) {
    float *p = (float *)malloc((size_t)n * sizeof(float));
    for (bt_frame i = 0; i < n; i++) p[i] = v;
    return p;
}

static void test_bucket_arithmetic(void) {
    const bt_frame N = 1000;
    float *a = ramp_alloc(N, 0.0f);
    const float *pcm[1] = { a };
    bt_peaks p;

    /* Rounds up: the tail of a stem is part of the stem. */
    BT_CHECK_EQI(bt_peaks_build(pcm, 1, N, 100, &p), BT_OK);
    BT_CHECK_EQI(p.buckets, 10);
    bt_peaks_free(&p);

    BT_CHECK_EQI(bt_peaks_build(pcm, 1, N, 300, &p), BT_OK);
    BT_CHECK_EQI(p.buckets, 4);            /* 300+300+300+100 */
    BT_CHECK_EQI(p.frames, N);
    BT_CHECK_EQI(p.frames_per_bucket, 300);
    bt_peaks_free(&p);
    BT_CHECK(p.min == NULL);

    free(a);
}

static void test_silence_and_full_scale(void) {
    const bt_frame N = 512;
    float *quiet = ramp_alloc(N, 0.0f);
    float *loud  = (float *)malloc((size_t)N * sizeof(float));
    for (bt_frame i = 0; i < N; i++) loud[i] = (i & 1) ? 1.0f : -1.0f;

    bt_peaks p;
    const float *q[1] = { quiet };
    BT_CHECK_EQI(bt_peaks_build(q, 1, N, 64, &p), BT_OK);
    for (int32_t k = 0; k < p.buckets; k++) {
        BT_CHECK_NEAR(p.min[k], 0.0, 1e-9);
        BT_CHECK_NEAR(p.max[k], 0.0, 1e-9);
    }
    bt_peaks_free(&p);

    const float *l[1] = { loud };
    BT_CHECK_EQI(bt_peaks_build(l, 1, N, 64, &p), BT_OK);
    for (int32_t k = 0; k < p.buckets; k++) {
        BT_CHECK_NEAR(p.min[k], -1.0, 1e-9);
        BT_CHECK_NEAR(p.max[k],  1.0, 1e-9);
    }
    bt_peaks_free(&p);

    free(quiet); free(loud);
}

static void test_a_single_transient_survives(void) {
    /* One sample of signal in 10000 of silence. An RMS or averaging envelope
     * would bury this; it is the whole reason min/max is the right choice. */
    const bt_frame N = 10000;
    float *a = ramp_alloc(N, 0.0f);
    a[7321] = 0.85f;
    const float *pcm[1] = { a };

    bt_peaks p;
    BT_CHECK_EQI(bt_peaks_build(pcm, 1, N, 100, &p), BT_OK);

    int32_t hit = 7321 / 100;
    BT_CHECK_NEAR(p.max[hit], 0.85, 1e-6);
    for (int32_t k = 0; k < p.buckets; k++)
        if (k != hit) BT_CHECK_NEAR(p.max[k], 0.0, 1e-9);

    /* And it survives being read back at a coarser zoom. */
    float lo[8], hi[8];
    BT_CHECK_EQI(bt_peaks_read(&p, 0, N, lo, hi, 8), BT_OK);
    float peak = 0.0f;
    for (int i = 0; i < 8; i++) if (hi[i] > peak) peak = hi[i];
    BT_CHECK_NEAR(peak, 0.85, 1e-6);

    bt_peaks_free(&p);
    free(a);
}

static void test_channels_take_extremes_not_the_sum(void) {
    /* Two channels in perfect opposition. Summing reads as silence, which is
     * a lie about exactly the thing being looked for. */
    const bt_frame N = 256;
    float *l = (float *)malloc((size_t)N * sizeof(float));
    float *r = (float *)malloc((size_t)N * sizeof(float));
    for (bt_frame i = 0; i < N; i++) { l[i] = 0.7f; r[i] = -0.7f; }
    const float *pcm[2] = { l, r };

    bt_peaks p;
    BT_CHECK_EQI(bt_peaks_build(pcm, 2, N, 64, &p), BT_OK);
    for (int32_t k = 0; k < p.buckets; k++) {
        BT_CHECK_NEAR(p.min[k], -0.7, 1e-6);
        BT_CHECK_NEAR(p.max[k],  0.7, 1e-6);
    }
    bt_peaks_free(&p);
    free(l); free(r);
}

static void test_range_matches_a_direct_scan(void) {
    const bt_frame N = 4096;
    float *a = (float *)malloc((size_t)N * sizeof(float));
    bt_lcg_seed(21);
    for (bt_frame i = 0; i < N; i++) a[i] = bt_lcg_sample();
    const float *pcm[1] = { a };

    /* A range query over the whole stem, at the cache's own resolution, must
     * agree with the cache bucket for bucket. */
    bt_peaks p;
    BT_CHECK_EQI(bt_peaks_build(pcm, 1, N, 64, &p), BT_OK);

    float *lo = (float *)malloc((size_t)p.buckets * sizeof(float));
    float *hi = (float *)malloc((size_t)p.buckets * sizeof(float));
    BT_CHECK_EQI(bt_peaks_range(pcm, 1, N, 0, N, lo, hi, p.buckets), BT_OK);
    for (int32_t k = 0; k < p.buckets; k++) {
        BT_CHECK_NEAR(lo[k], p.min[k], 1e-9);
        BT_CHECK_NEAR(hi[k], p.max[k], 1e-9);
    }
    free(lo); free(hi);
    bt_peaks_free(&p);

    /* Zoomed in: a 200-frame window across 100 buckets, so most buckets hold
     * one or two samples. The extremes must still be the true extremes. */
    float z_lo[100], z_hi[100];
    BT_CHECK_EQI(bt_peaks_range(pcm, 1, N, 1000, 1200, z_lo, z_hi, 100), BT_OK);
    float lo_all = 1.0f, hi_all = -1.0f;
    for (bt_frame i = 1000; i < 1200; i++) {
        if (a[i] < lo_all) lo_all = a[i];
        if (a[i] > hi_all) hi_all = a[i];
    }
    float lo_seen = 1.0f, hi_seen = -1.0f;
    for (int i = 0; i < 100; i++) {
        if (z_lo[i] < lo_seen) lo_seen = z_lo[i];
        if (z_hi[i] > hi_seen) hi_seen = z_hi[i];
    }
    BT_CHECK_NEAR(lo_seen, lo_all, 1e-9);
    BT_CHECK_NEAR(hi_seen, hi_all, 1e-9);

    free(a);
}

static void test_outside_the_stem_reads_as_silence(void) {
    /* Normal when a stem is nudged: the view extends past one end. That
     * should draw nothing there, not fail and not read memory it does not
     * own. */
    const bt_frame N = 500;
    float *a = ramp_alloc(N, 0.5f);
    const float *pcm[1] = { a };

    float lo[10], hi[10];
    BT_CHECK_EQI(bt_peaks_range(pcm, 1, N, -1000, -500, lo, hi, 10), BT_OK);
    for (int i = 0; i < 10; i++) { BT_CHECK_NEAR(lo[i], 0.0, 1e-9);
                                   BT_CHECK_NEAR(hi[i], 0.0, 1e-9); }

    BT_CHECK_EQI(bt_peaks_range(pcm, 1, N, 400, 900, lo, hi, 10), BT_OK);
    /* First half is inside the stem, second half past its end. */
    BT_CHECK_NEAR(hi[0], 0.5, 1e-6);
    BT_CHECK_NEAR(hi[9], 0.0, 1e-9);

    free(a);
}

static void test_bad_arguments(void) {
    float x = 0.0f;
    const float *pcm[1] = { &x };
    bt_peaks p;
    float lo[4], hi[4];

    BT_CHECK(bt_peaks_build(NULL, 1, 10, 4, &p) != BT_OK);
    BT_CHECK(bt_peaks_build(pcm, 0, 10, 4, &p) != BT_OK);
    BT_CHECK(bt_peaks_build(pcm, 1, 0, 4, &p) != BT_OK);
    BT_CHECK(bt_peaks_build(pcm, 1, 10, 0, &p) != BT_OK);
    BT_CHECK(bt_peaks_range(pcm, 1, 1, 10, 10, lo, hi, 4) != BT_OK);
    BT_CHECK(bt_peaks_range(pcm, 1, 1, 10, 5, lo, hi, 4) != BT_OK);
    BT_CHECK(bt_peaks_read(NULL, 0, 10, lo, hi, 4) != BT_OK);
    bt_peaks_free(NULL);
}

int main(void) {
    BT_RUN(test_bucket_arithmetic);
    BT_RUN(test_silence_and_full_scale);
    BT_RUN(test_a_single_transient_survives);
    BT_RUN(test_channels_take_extremes_not_the_sum);
    BT_RUN(test_range_matches_a_direct_scan);
    BT_RUN(test_outside_the_stem_reads_as_silence);
    BT_RUN(test_bad_arguments);
    BT_REPORT();
}
