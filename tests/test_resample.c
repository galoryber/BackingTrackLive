/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * These tests measure the filter the resampler actually produces rather than
 * asserting that the code looks correct. A resampler that is subtly wrong
 * still produces plausible-looking audio; the failure shows up as a dull or
 * slightly gritty backing track that nobody can quite explain.
 */
#include "bt_test.h"
#include "backtrack/bt_resample.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static float *gen_sine(bt_frame n, double hz, double amp, int32_t sr) {
    float *x = (float *)malloc((size_t)n * sizeof(float));
    for (bt_frame i = 0; i < n; i++)
        x[i] = (float)(amp * sin(2.0 * M_PI * hz * (double)i / (double)sr));
    return x;
}

/* Amplitude of the component at `hz`, measured over a Hann-windowed interior
 * span so the filter's edge ramp does not pollute the estimate. */
static double mag_at(const float *x, bt_frame from, bt_frame count,
                     double hz, int32_t sr) {
    double re = 0.0, im = 0.0, wsum = 0.0;
    for (bt_frame i = 0; i < count; i++) {
        double w  = 0.5 - 0.5 * cos(2.0 * M_PI * (double)i / (double)(count - 1));
        double t  = (double)(from + i) / (double)sr;
        double ph = 2.0 * M_PI * hz * t;
        re   += x[from + i] * w * cos(ph);
        im   -= x[from + i] * w * sin(ph);
        wsum += w;
    }
    return 2.0 * sqrt(re * re + im * im) / wsum;
}

static double db(double x) { return 20.0 * log10(x < 1e-12 ? 1e-12 : x); }

static void free_planar(float **p, int32_t ch) {
    for (int32_t c = 0; c < ch; c++) free(p[c]);
    free(p);
}

/* ------------------------------------------------------------------ tests */

static void test_lengths(void) {
    BT_CHECK_EQI(bt_resample_out_frames(44100, 44100, 48000), 48000);
    BT_CHECK_EQI(bt_resample_out_frames(48000, 48000, 44100), 44100);
    BT_CHECK_EQI(bt_resample_out_frames(1000,  48000, 48000), 1000);
    BT_CHECK_EQI(bt_resample_out_frames(48000, 48000, 96000), 96000);
    BT_CHECK_EQI(bt_resample_out_frames(96000, 96000, 48000), 48000);
}

static void test_identity_is_a_copy(void) {
    const bt_frame N = 1024;
    float *x = gen_sine(N, 1000.0, 0.5, 48000);
    const float *in[1] = { x };

    float **out = NULL;
    bt_frame n = 0;
    BT_CHECK_EQI(bt_resample_planar(in, 1, N, 48000, 48000, &out, &n), BT_OK);
    BT_CHECK_EQI(n, N);
    BT_CHECK(memcmp(out[0], x, (size_t)N * sizeof(float)) == 0);

    free_planar(out, 1);
    free(x);
}

static void test_dc_is_preserved(void) {
    /* Each polyphase kernel is normalised to unity DC gain, so a constant in
     * must come out constant - no ripple, no level shift. */
    const bt_frame N = 20000;
    float *x = (float *)malloc((size_t)N * sizeof(float));
    for (bt_frame i = 0; i < N; i++) x[i] = 0.5f;
    const float *in[1] = { x };

    float **out = NULL;
    bt_frame n = 0;
    BT_CHECK_EQI(bt_resample_planar(in, 1, N, 44100, 48000, &out, &n), BT_OK);

    for (bt_frame i = 2000; i < n - 2000; i += 97)
        BT_CHECK_NEAR(out[0][i], 0.5, 1e-5);

    free_planar(out, 1);
    free(x);
}

static void test_passband_is_flat(void) {
    /* 44.1k -> 48k, the conversion that will actually happen most often. */
    const int32_t IN = 44100, OUT = 48000;
    const bt_frame N = IN * 2;
    const double freqs[] = { 100.0, 440.0, 1000.0, 3000.0, 6000.0, 10000.0 };

    for (size_t k = 0; k < sizeof(freqs) / sizeof(freqs[0]); k++) {
        float *x = gen_sine(N, freqs[k], 0.5, IN);
        const float *in[1] = { x };

        float **out = NULL;
        bt_frame n = 0;
        BT_CHECK_EQI(bt_resample_planar(in, 1, N, IN, OUT, &out, &n), BT_OK);

        double m  = mag_at(out[0], 4000, n - 8000, freqs[k], OUT);
        double err = db(m / 0.5);
        bt_checks++;
        if (fabs(err) > 0.1) {
            bt_fails++;
            fprintf(stderr, "  FAIL passband at %.0f Hz: %+.3f dB\n", freqs[k], err);
        }
        free_planar(out, 1);
        free(x);
    }
}

static void test_downsample_rejects_aliases(void) {
    /* 48k -> 24k. Nyquist drops to 12 kHz, so an 18 kHz tone must be filtered
     * away, NOT folded down to 6 kHz. Aliasing is the one resampler failure
     * that sounds like a broken recording rather than a dull one. */
    const int32_t IN = 48000, OUT = 24000;
    const bt_frame N = IN;
    float *x = gen_sine(N, 18000.0, 0.5, IN);
    const float *in[1] = { x };

    float **out = NULL;
    bt_frame n = 0;
    BT_CHECK_EQI(bt_resample_planar(in, 1, N, IN, OUT, &out, &n), BT_OK);

    double alias = mag_at(out[0], 3000, n - 6000, 6000.0, OUT);
    double rej   = db(alias / 0.5);
    bt_checks++;
    if (rej > -80.0) {
        bt_fails++;
        fprintf(stderr, "  FAIL 18 kHz aliased to 6 kHz at %.1f dB "
                        "(want <= -80 dB)\n", rej);
    }

    /* And the whole output should be near-silent, not merely alias-free. */
    double peak = 0.0;
    for (bt_frame i = 3000; i < n - 3000; i++)
        if (fabs((double)out[0][i]) > peak) peak = fabs((double)out[0][i]);
    bt_checks++;
    if (db(peak / 0.5) > -80.0) {
        bt_fails++;
        fprintf(stderr, "  FAIL stopband leak: peak %.1f dB\n", db(peak / 0.5));
    }

    free_planar(out, 1);
    free(x);
}

static void test_downsample_keeps_passband(void) {
    /* Same conversion, a tone that belongs: 1 kHz must survive intact. */
    const int32_t IN = 48000, OUT = 24000;
    const bt_frame N = IN;
    float *x = gen_sine(N, 1000.0, 0.5, IN);
    const float *in[1] = { x };

    float **out = NULL;
    bt_frame n = 0;
    BT_CHECK_EQI(bt_resample_planar(in, 1, N, IN, OUT, &out, &n), BT_OK);

    double m = mag_at(out[0], 3000, n - 6000, 1000.0, OUT);
    BT_CHECK_NEAR(db(m / 0.5), 0.0, 0.1);

    free_planar(out, 1);
    free(x);
}

static void test_roundtrip(void) {
    /* 44.1k -> 48k -> 44.1k should return what went in, for anything
     * comfortably inside the passband. */
    const bt_frame N = 44100 * 2;
    float *x = gen_sine(N, 997.0, 0.5, 44100);
    const float *in[1] = { x };

    float **up = NULL;   bt_frame nu = 0;
    BT_CHECK_EQI(bt_resample_planar(in, 1, N, 44100, 48000, &up, &nu), BT_OK);

    const float *mid[1] = { up[0] };
    float **back = NULL; bt_frame nb = 0;
    BT_CHECK_EQI(bt_resample_planar(mid, 1, nu, 48000, 44100, &back, &nb), BT_OK);

    double m = mag_at(back[0], 5000, nb - 10000, 997.0, 44100);
    BT_CHECK_NEAR(db(m / 0.5), 0.0, 0.15);

    /* Residual against the original, over the interior. */
    double err = 0.0;
    bt_frame cnt = 0;
    for (bt_frame i = 5000; i < N - 5000 && i < nb - 5000; i++) {
        double d = (double)back[0][i] - (double)x[i];
        err += d * d;
        cnt++;
    }
    double rms = sqrt(err / (double)cnt);
    bt_checks++;
    if (db(rms / 0.5) > -60.0) {
        bt_fails++;
        fprintf(stderr, "  FAIL roundtrip residual %.1f dB (want <= -60)\n",
                db(rms / 0.5));
    }

    free_planar(back, 1);
    free_planar(up, 1);
    free(x);
}

static void test_channels_are_independent(void) {
    const bt_frame N = 20000;
    float *a = gen_sine(N, 1000.0, 0.5, 44100);
    float *b = (float *)calloc((size_t)N, sizeof(float));
    const float *in[2] = { a, b };

    float **out = NULL;
    bt_frame n = 0;
    BT_CHECK_EQI(bt_resample_planar(in, 2, N, 44100, 48000, &out, &n), BT_OK);

    double sig = mag_at(out[0], 3000, n - 6000, 1000.0, 48000);
    BT_CHECK_NEAR(db(sig / 0.5), 0.0, 0.1);

    for (bt_frame i = 0; i < n; i++) BT_CHECK(fabsf(out[1][i]) < 1e-9f);

    free_planar(out, 2);
    free(a); free(b);
}

static void test_bad_arguments(void) {
    float x = 0.0f;
    const float *in[1] = { &x };
    float **out = NULL;
    bt_frame n = 0;
    BT_CHECK(bt_resample_planar(NULL, 1, 1, 48000, 44100, &out, &n) != BT_OK);
    BT_CHECK(bt_resample_planar(in, 0, 1, 48000, 44100, &out, &n) != BT_OK);
    BT_CHECK(bt_resample_planar(in, 1, 0, 48000, 44100, &out, &n) != BT_OK);
    BT_CHECK(bt_resample_planar(in, 1, 1, 0, 44100, &out, &n) != BT_OK);
    BT_CHECK(bt_resample_planar(in, 1, 1, 48000, 0, &out, &n) != BT_OK);
    BT_CHECK_EQI(bt_resample_out_frames(0, 48000, 44100), 0);
}

int main(void) {
    BT_RUN(test_lengths);
    BT_RUN(test_identity_is_a_copy);
    BT_RUN(test_dc_is_preserved);
    BT_RUN(test_passband_is_flat);
    BT_RUN(test_downsample_rejects_aliases);
    BT_RUN(test_downsample_keeps_passband);
    BT_RUN(test_roundtrip);
    BT_RUN(test_channels_are_independent);
    BT_RUN(test_bad_arguments);
    BT_REPORT();
}
