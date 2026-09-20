/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * Windowed-sinc polyphase resampler, used at load time only.
 */
#include "backtrack/bt_resample.h"

#include <stdlib.h>
#include <string.h>
#define _USE_MATH_DEFINES
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Taps per side at unity ratio. 32 puts the transition band comfortably above
 * anything musical and the stopband near the window's -92 dB floor. Offline,
 * so the cost is a few seconds per song at load, not a deadline. */
#define BT_TAPS_PER_SIDE 32

/* Bounds. `half` grows when downsampling (the cutoff drops), and the phase
 * count grows with unusual rate pairs; both are capped so a malformed or
 * hostile file cannot ask for an enormous filter bank. */
#define BT_MAX_HALF   512
#define BT_MAX_PHASES 8192

static int64_t gcd64(int64_t a, int64_t b) {
    while (b) { int64_t t = a % b; a = b; b = t; }
    return a < 0 ? -a : a;
}

static double sinc(double x) {
    if (x > -1e-9 && x < 1e-9) return 1.0;
    double px = M_PI * x;
    return sin(px) / px;
}

/* Blackman-Harris, evaluated over u in [-1, 1]. Chosen over Kaiser because its
 * ~-92 dB sidelobes are far more than this needs and it costs three cosines
 * instead of a Bessel function. */
static double window(double u) {
    if (u <= -1.0 || u >= 1.0) return 0.0;
    double t = (u + 1.0) * 0.5;
    return 0.35875
         - 0.48829 * cos(2.0 * M_PI * t)
         + 0.14128 * cos(4.0 * M_PI * t)
         - 0.01168 * cos(6.0 * M_PI * t);
}

bt_frame bt_resample_out_frames(bt_frame in_frames, int32_t in_rate, int32_t out_rate) {
    if (in_frames <= 0 || in_rate <= 0 || out_rate <= 0) return 0;
    if (in_rate == out_rate) return in_frames;
    int64_t g    = gcd64(out_rate, in_rate);
    int64_t up   = out_rate / g;
    int64_t down = in_rate  / g;
    return (bt_frame)((( (int64_t)in_frames * up) + down - 1) / down);
}

static void free_planar(float **p, int32_t channels) {
    if (!p) return;
    for (int32_t c = 0; c < channels; c++) free(p[c]);
    free(p);
}

bt_err bt_resample_planar(const float *const *in, int32_t channels,
                          bt_frame in_frames, int32_t in_rate, int32_t out_rate,
                          float ***out, bt_frame *out_frames) {
    if (!in || !out || !out_frames || channels <= 0) return BT_ERR_RANGE;
    if (in_frames <= 0 || in_rate <= 0 || out_rate <= 0) return BT_ERR_RANGE;

    *out = NULL;
    *out_frames = 0;

    /* ---- Straight copy when the rates already agree. ---------------- */
    if (in_rate == out_rate) {
        float **dst = (float **)calloc((size_t)channels, sizeof(*dst));
        if (!dst) return BT_ERR_ALLOC;
        for (int32_t c = 0; c < channels; c++) {
            dst[c] = (float *)malloc((size_t)in_frames * sizeof(float));
            if (!dst[c]) { free_planar(dst, channels); return BT_ERR_ALLOC; }
            memcpy(dst[c], in[c], (size_t)in_frames * sizeof(float));
        }
        *out = dst;
        *out_frames = in_frames;
        return BT_OK;
    }

    const int64_t g    = gcd64(out_rate, in_rate);
    const int64_t up   = out_rate / g;     /* output frames per `down` input */
    const int64_t down = in_rate  / g;
    if (up > BT_MAX_PHASES) return BT_ERR_RANGE;

    /* Cutoff, in cycles per input sample. When downsampling it must drop to
     * the output Nyquist or the discarded band folds back as aliasing - which
     * is the entire reason this is a filter and not an interpolation. */
    const double ratio = (double)out_rate / (double)in_rate;
    const double fc    = (ratio < 1.0) ? 0.5 * ratio : 0.5;

    int64_t half = (int64_t)ceil((double)BT_TAPS_PER_SIDE / (2.0 * fc));
    if (half < 1) half = 1;
    if (half > BT_MAX_HALF) half = BT_MAX_HALF;
    const int32_t taps = (int32_t)(2 * half);

    /* ---- Precompute one kernel per phase. ---------------------------- */
    double *bank = (double *)malloc((size_t)up * (size_t)taps * sizeof(double));
    if (!bank) return BT_ERR_ALLOC;

    for (int64_t p = 0; p < up; p++) {
        double  frac = (double)p / (double)up;   /* sub-sample offset      */
        double *k    = bank + (size_t)p * (size_t)taps;
        double  sum  = 0.0;

        for (int32_t t = 0; t < taps; t++) {
            /* Tap t covers input index (i_int + t - half + 1); its distance
             * from the exact resampling position is that minus `frac`. */
            double x = (double)(t - half + 1) - frac;
            double v = 2.0 * fc * sinc(2.0 * fc * x) * window(x / (double)half);
            k[t] = v;
            sum += v;
        }
        /* Normalise each phase to unity DC gain. Cheaper and more robust than
         * trusting the analytic gain, and it makes the DC test exact. */
        if (sum != 0.0) {
            double inv = 1.0 / sum;
            for (int32_t t = 0; t < taps; t++) k[t] *= inv;
        }
    }

    const bt_frame n_out = bt_resample_out_frames(in_frames, in_rate, out_rate);

    float **dst = (float **)calloc((size_t)channels, sizeof(*dst));
    if (!dst) { free(bank); return BT_ERR_ALLOC; }
    for (int32_t c = 0; c < channels; c++) {
        dst[c] = (float *)calloc((size_t)n_out, sizeof(float));
        if (!dst[c]) { free_planar(dst, channels); free(bank); return BT_ERR_ALLOC; }
    }

    /* ---- Convolve. ---------------------------------------------------- */
    for (bt_frame j = 0; j < n_out; j++) {
        /* Input position of output frame j is j*down/up, split into an
         * integer sample and a phase index - no floating-point accumulation,
         * so position is exact however long the file is. */
        int64_t q     = (int64_t)j * down;
        int64_t i_int = q / up;
        int64_t phase = q % up;

        const double *k = bank + (size_t)phase * (size_t)taps;
        int64_t start = i_int - half + 1;

        for (int32_t c = 0; c < channels; c++) {
            const float *src = in[c];
            double acc = 0.0;
            for (int32_t t = 0; t < taps; t++) {
                int64_t i = start + t;
                /* Outside the signal is silence. Correct for a finite input,
                 * and stems begin and end in silence regardless. */
                if (i < 0 || i >= in_frames) continue;
                acc += (double)src[i] * k[t];
            }
            dst[c][j] = (float)acc;
        }
    }

    free(bank);
    *out = dst;
    *out_frames = n_out;
    return BT_OK;
}
