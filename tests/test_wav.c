/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0. */
#include "bt_test.h"
#include "backtrack/bt_wav.h"

static void put32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)v;         p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}

static void put16(unsigned char *p, uint16_t v) {
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
}

/* Builds a canonical 44-byte-header WAV around `payload`. */
static size_t build_wav(unsigned char *out, uint16_t fmt, uint16_t channels,
                        uint16_t bits, uint32_t rate,
                        const unsigned char *payload, size_t plen) {
    uint32_t block = (uint32_t)(bits / 8) * channels;
    memcpy(out, "RIFF", 4);
    put32(out + 4, (uint32_t)(36 + plen));
    memcpy(out + 8, "WAVE", 4);
    memcpy(out + 12, "fmt ", 4);
    put32(out + 16, 16);
    put16(out + 20, fmt);
    put16(out + 22, channels);
    put32(out + 24, rate);
    put32(out + 28, rate * block);
    put16(out + 32, (uint16_t)block);
    put16(out + 34, bits);
    memcpy(out + 36, "data", 4);
    put32(out + 40, (uint32_t)plen);
    memcpy(out + 44, payload, plen);
    return 44 + plen;
}

static void test_pcm16(void) {
    unsigned char pay[8];
    put16(pay + 0, 0x0000);            /* L =  0.0  */
    put16(pay + 2, 0x4000);            /* R =  0.5  */
    put16(pay + 4, 0x8000);            /* L = -1.0  */
    put16(pay + 6, 0xC000);            /* R = -0.5  */

    unsigned char buf[256];
    size_t n = build_wav(buf, 1, 2, 16, 48000, pay, sizeof(pay));

    bt_wav w;
    BT_CHECK_EQI(bt_wav_decode(buf, n, &w), BT_OK);
    BT_CHECK_EQI(w.channels, 2);
    BT_CHECK_EQI(w.sample_rate, 48000);
    BT_CHECK_EQI(w.frames, 2);
    BT_CHECK_NEAR(w.pcm[0][0],  0.0, 1e-6);
    BT_CHECK_NEAR(w.pcm[1][0],  0.5, 1e-4);
    BT_CHECK_NEAR(w.pcm[0][1], -1.0, 1e-6);
    BT_CHECK_NEAR(w.pcm[1][1], -0.5, 1e-4);
    bt_wav_free(&w);
    BT_CHECK(w.pcm == NULL);
}

static void test_pcm24(void) {
    unsigned char pay[6] = { 0x00, 0x00, 0x40,    /*  0.5 */
                             0x00, 0x00, 0xC0 };  /* -0.5 */
    unsigned char buf[256];
    size_t n = build_wav(buf, 1, 1, 24, 44100, pay, sizeof(pay));

    bt_wav w;
    BT_CHECK_EQI(bt_wav_decode(buf, n, &w), BT_OK);
    BT_CHECK_EQI(w.frames, 2);
    BT_CHECK_EQI(w.sample_rate, 44100);
    BT_CHECK_NEAR(w.pcm[0][0],  0.5, 1e-5);
    BT_CHECK_NEAR(w.pcm[0][1], -0.5, 1e-5);
    bt_wav_free(&w);
}

static void test_float32(void) {
    float vals[2] = { 0.25f, -0.75f };
    unsigned char pay[8];
    memcpy(pay, vals, sizeof(vals));

    unsigned char buf[256];
    size_t n = build_wav(buf, 3, 1, 32, 48000, pay, sizeof(pay));

    bt_wav w;
    BT_CHECK_EQI(bt_wav_decode(buf, n, &w), BT_OK);
    BT_CHECK_NEAR(w.pcm[0][0],  0.25, 1e-7);
    BT_CHECK_NEAR(w.pcm[0][1], -0.75, 1e-7);
    bt_wav_free(&w);
}

static void test_roundtrip_file(void) {
    const bt_frame N = 512;
    float *l = (float *)malloc((size_t)N * sizeof(float));
    float *r = (float *)malloc((size_t)N * sizeof(float));
    bt_lcg_seed(7);
    for (bt_frame i = 0; i < N; i++) { l[i] = bt_lcg_sample(); r[i] = bt_lcg_sample(); }

    const float *pl[2] = { l, r };
    const char *path = "test_roundtrip.wav";
    BT_CHECK_EQI(bt_wav_write_file(path, pl, 2, 48000, N), BT_OK);

    bt_wav w;
    BT_CHECK_EQI(bt_wav_read_file(path, &w), BT_OK);
    BT_CHECK_EQI(w.frames, N);
    BT_CHECK_EQI(w.channels, 2);
    for (bt_frame i = 0; i < N; i++) {
        BT_CHECK_NEAR(w.pcm[0][i], l[i], 1.0 / 8388607.0 * 2.0);
        BT_CHECK_NEAR(w.pcm[1][i], r[i], 1.0 / 8388607.0 * 2.0);
    }
    bt_wav_free(&w);
    free(l); free(r);
    remove(path);
}

static void test_malformed(void) {
    unsigned char pay[4] = { 0, 0, 0, 0 };
    unsigned char buf[256];
    size_t n = build_wav(buf, 1, 2, 16, 48000, pay, sizeof(pay));

    bt_wav w;

    /* Not a RIFF file. */
    BT_CHECK(bt_wav_decode("not a wav at all", 16, &w) != BT_OK);

    /* Truncated below the RIFF header. */
    BT_CHECK(bt_wav_decode(buf, 8, &w) != BT_OK);

    /* Unsupported bit depth. */
    unsigned char odd[256];
    size_t m = build_wav(odd, 1, 1, 12, 48000, pay, sizeof(pay));
    BT_CHECK(bt_wav_decode(odd, m, &w) != BT_OK);

    /* Zero channels. */
    unsigned char zc[256];
    size_t z = build_wav(zc, 1, 0, 16, 48000, pay, sizeof(pay));
    BT_CHECK(bt_wav_decode(zc, z, &w) != BT_OK);

    /* A data chunk claiming more bytes than the file holds is clamped, not
     * trusted - truncated downloads are common and should still open. */
    put32(buf + 40, 0xFFFFFF00u);
    BT_CHECK_EQI(bt_wav_decode(buf, n, &w), BT_OK);
    BT_CHECK_EQI(w.frames, 1);
    bt_wav_free(&w);
}

int main(void) {
    BT_RUN(test_pcm16);
    BT_RUN(test_pcm24);
    BT_RUN(test_float32);
    BT_RUN(test_roundtrip_file);
    BT_RUN(test_malformed);
    BT_REPORT();
}
