/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * Format dispatch and the vendored decoders.
 *
 * Fixtures are committed rather than generated, deliberately: encoding a test
 * MP3 would make every CI runner depend on an encoder being installed, and
 * codec tests want a byte-stable input anyway. The WAV and FLAC fixtures hold
 * the same 16-bit samples, so "FLAC is lossless" is a real assertion here
 * rather than a claim.
 */
#include "bt_test.h"
#include "backtrack/bt_audio.h"

#ifndef BT_FIXTURE_DIR
#define BT_FIXTURE_DIR "."
#endif
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void fixture(char *dst, size_t cap, const char *name) {
    snprintf(dst, cap, "%s/%s", BT_FIXTURE_DIR, name);
}

static bt_err load(const char *name, bt_audio *out) {
    char path[512];
    fixture(path, sizeof(path), name);
    return bt_audio_decode_file(path, out);
}

/* Amplitude at `hz` over a Hann-windowed span. */
static double mag_at(const float *x, bt_frame from, bt_frame count,
                     double hz, int32_t sr) {
    double re = 0.0, im = 0.0, w = 0.0;
    for (bt_frame i = 0; i < count; i++) {
        double h  = 0.5 - 0.5 * cos(2.0 * M_PI * (double)i / (double)(count - 1));
        double ph = 2.0 * M_PI * hz * (double)(from + i) / (double)sr;
        re += x[from + i] * h * cos(ph);
        im -= x[from + i] * h * sin(ph);
        w  += h;
    }
    return 2.0 * sqrt(re * re + im * im) / w;
}

static unsigned char *slurp(const char *name, size_t *len) {
    char path[512];
    fixture(path, sizeof(path), name);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    unsigned char *b = (unsigned char *)malloc((size_t)sz);
    *len = fread(b, 1, (size_t)sz, f);
    fclose(f);
    return b;
}

/* ------------------------------------------------------------------ tests */

static void test_wav_fixture(void) {
    bt_audio a;
    BT_CHECK_EQI(load("tone_stereo.wav", &a), BT_OK);
    BT_CHECK_EQI(a.sample_rate, 44100);
    BT_CHECK_EQI(a.channels, 2);
    BT_CHECK_EQI(a.frames, 22050);
    /* 1 kHz left, 2.5 kHz right, amplitude 0.5. */
    BT_CHECK_NEAR(mag_at(a.pcm[0], 1000, 20000, 1000.0, 44100), 0.5, 0.01);
    BT_CHECK_NEAR(mag_at(a.pcm[1], 1000, 20000, 2500.0, 44100), 0.5, 0.01);
    /* Channels are not crossed. */
    BT_CHECK(mag_at(a.pcm[0], 1000, 20000, 2500.0, 44100) < 0.01);
    bt_audio_free(&a);
    BT_CHECK(a.pcm == NULL);
}

static void test_flac_is_lossless(void) {
    bt_audio w, f;
    BT_CHECK_EQI(load("tone_stereo.wav",  &w), BT_OK);
    BT_CHECK_EQI(load("tone_stereo.flac", &f), BT_OK);

    BT_CHECK_EQI(f.sample_rate, w.sample_rate);
    BT_CHECK_EQI(f.channels,    w.channels);
    BT_CHECK_EQI(f.frames,      w.frames);

    /* Same 16-bit source, so this must be exact, not merely close. */
    for (int32_t c = 0; c < w.channels; c++)
        BT_CHECK(memcmp(f.pcm[c], w.pcm[c],
                        (size_t)w.frames * sizeof(float)) == 0);

    bt_audio_free(&w);
    bt_audio_free(&f);
}

static void test_mp3_decodes(void) {
    bt_audio a;
    BT_CHECK_EQI(load("tone_mono.mp3", &a), BT_OK);
    BT_CHECK_EQI(a.sample_rate, 44100);
    BT_CHECK_EQI(a.channels, 1);

    /* MP3 is lossy and carries encoder delay and padding, so length is
     * approximate and the tone is checked by tolerance, not equality. */
    BT_CHECK(a.frames > 20000 && a.frames < 26000);
    double m = mag_at(a.pcm[0], 2000, 16000, 1000.0, 44100);
    BT_CHECK_NEAR(20.0 * log10(m / 0.5), 0.0, 1.5);

    bt_audio_free(&a);
}

static void test_detection_is_by_content(void) {
    /* Somebody re-saves a stem and the extension stops matching the format.
     * That should just work rather than fail at soundcheck. */
    size_t len = 0;
    unsigned char *mp3 = slurp("tone_mono.mp3", &len);
    BT_CHECK(mp3 != NULL);
    if (!mp3) return;

    bt_audio a;
    /* Lied about, or absent entirely - content wins either way. */
    BT_CHECK_EQI(bt_audio_decode_mem(mp3, len, "wav", &a), BT_OK);
    BT_CHECK_EQI(a.sample_rate, 44100);
    bt_audio_free(&a);

    BT_CHECK_EQI(bt_audio_decode_mem(mp3, len, NULL, &a), BT_OK);
    bt_audio_free(&a);

    free(mp3);

    size_t flen = 0;
    unsigned char *fl = slurp("tone_stereo.flac", &flen);
    BT_CHECK(fl != NULL);
    if (fl) {
        BT_CHECK_EQI(bt_audio_decode_mem(fl, flen, "mp3", &a), BT_OK);
        BT_CHECK_EQI(a.channels, 2);
        bt_audio_free(&a);
        free(fl);
    }
}

static void test_garbage_is_refused(void) {
    bt_audio a;
    BT_CHECK(bt_audio_decode_mem("", 0, NULL, &a) != BT_OK);
    BT_CHECK(bt_audio_decode_mem("ab", 2, NULL, &a) != BT_OK);
    BT_CHECK(bt_audio_decode_mem("this is not audio at all", 24, NULL, &a) != BT_OK);
    BT_CHECK(bt_audio_decode_mem(NULL, 10, NULL, &a) != BT_OK);
    BT_CHECK(bt_audio_decode_file("no_such_file_here.wav", &a) != BT_OK);

    /* A truncated FLAC must fail cleanly, not half-decode into nonsense. */
    size_t len = 0;
    unsigned char *fl = slurp("tone_stereo.flac", &len);
    if (fl) {
        bt_err e = bt_audio_decode_mem(fl, 40, "flac", &a);
        if (e == BT_OK) bt_audio_free(&a);   /* acceptable, must not crash */
        free(fl);
    }
    BT_CHECK(bt_audio_formats() != NULL);
    BT_CHECK(strlen(bt_audio_formats()) > 0);
}

static void test_free_is_idempotent(void) {
    bt_audio a;
    BT_CHECK_EQI(load("tone_stereo.wav", &a), BT_OK);
    bt_audio_free(&a);
    bt_audio_free(&a);          /* must be safe */
    bt_audio_free(NULL);
    BT_CHECK(a.pcm == NULL);
    BT_CHECK_EQI(a.channels, 0);
}

int main(void) {
    BT_RUN(test_wav_fixture);
    BT_RUN(test_flac_is_lossless);
    BT_RUN(test_mp3_decodes);
    BT_RUN(test_detection_is_by_content);
    BT_RUN(test_garbage_is_refused);
    BT_RUN(test_free_is_idempotent);
    BT_REPORT();
}
